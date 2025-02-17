/**
 * @file Example of how to connect to a python WebRTC server and send/receive
 * H.264 video frames using C++.
 */
#include <chrono>
#include <cstdio>
#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <rtc/rtc.hpp>
#include <thread>
#include <vector>

using json = nlohmann::json;

// Default media pass-through pipeline prompt for the AI WebRTC Server.
static const std::string defaultPipelinePrompt = R"({
  "1": {
    "inputs": { "image": "sampled_frame.jpg" }
  },
  "2": {
    "inputs": { "images": ["12", 0] }
  }
})";

/**
 * Send the SDP offer to the AI WebRTC Server.
 */
std::string sendSDPToServer(const std::string &sdp) {
  httplib::Client cli("http://0.0.0.0:8888");

  json request;
  request["offer"] = {{"sdp", sdp}, {"type", "offer"}};
  json prompt = json::parse(defaultPipelinePrompt);
  request["prompt"] = prompt;

  auto res = cli.Post("/offer", request.dump(), "application/json");
  return (res && res->status == 200) ? res->body : "";
}

/**
 * Dummy decode function.
 */
static cv::Mat decodeH264ToMat(const uint8_t *data, size_t size) {
  std::cout << "[C++] (Dummy) decodeH264ToMat called with size=" << size
            << std::endl;

  // Return an empty Mat to allow compilation/testing
  return cv::Mat();
}

int main() {
  rtc::InitLogger(rtc::LogLevel::Info);
  std::cout << "Starting AI WebRTC Client..." << std::endl;

  // Create PeerConnection.
  rtc::Configuration config;
  config.iceServers.emplace_back("stun:stun.l.google.com:19302");
  auto pc = std::make_shared<rtc::PeerConnection>(config);

  // Add video channel (SendRecv).
  const rtc::SSRC ssrc = 42;
  const uint8_t payloadType = 96;
  rtc::Description::Video media("video", rtc::Description::Direction::SendRecv);

  // Include typical H.264 parameters to ensure compatibility.
  media.addH264Codec(
      payloadType,
      "packetization-mode=1;profile-level-id=42e01f;level-asymmetry-allowed=1");
  media.addSSRC(ssrc, "video-track");
  auto track = pc->addTrack(media);

  // Set up H.264 RTP packetizer for *outgoing* frames.
  auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
      ssrc, "video-send", payloadType, 90000);
  auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
      rtc::NalUnit::Separator::LongStartSequence, rtpConfig, 1200);
  track->setMediaHandler(packetizer);

  // PeerConnection and track event listeners.
  pc->onLocalDescription([&pc](rtc::Description sdp) {
    std::cout << "[Local Description] " << std::string(sdp) << std::endl;
  });
  pc->onLocalCandidate([](rtc::Candidate candidate) {
    std::cout << "[Local Candidate] " << candidate << std::endl;
  });
  bool connectionEstablished = false;
  pc->onStateChange([&](rtc::PeerConnection::State state) {
    std::cout << "[PeerConnection State Change] New state: " << state
              << std::endl;
    if (state == rtc::PeerConnection::State::Connected) {
      connectionEstablished = true;
    }
  });
  pc->onIceStateChange([](rtc::PeerConnection::IceState state) {
    std::cout << "[ICE State Change] New state: " << state << std::endl;
  });
  pc->onSignalingStateChange([](rtc::PeerConnection::SignalingState state) {
    std::cout << "[Signaling State Change] New state: " << state << std::endl;
  });
  track->onFrame([](rtc::binary encodedData, rtc::FrameInfo info) {
    std::cout << "[C++] Received frame, size=" << encodedData.size()
              << std::endl;
  });
  pc->onTrack([](std::shared_ptr<rtc::Track> inboundTrack) {
    std::cout << "[C++] Received remote track with mid: " << inboundTrack->mid()
              << std::endl;
  });

  // Implement non-trickle ICE nogotiation.
  pc->onGatheringStateChange([&pc](rtc::PeerConnection::GatheringState state) {
    std::cout << "[C++] Gathering State: " << state << std::endl;
    if (state == rtc::PeerConnection::GatheringState::Complete) {
      std::cout << "[C++] All ICE candidates gathered." << std::endl;
      auto desc = pc->localDescription();
      if (!desc) {
        std::cerr << "[C++] No local description!" << std::endl;
        return;
      }

      // Send the SDP offer to the python WebRTC server.
      std::string serverResponse =
          sendSDPToServer(static_cast<std::string>(*desc));
      if (serverResponse.empty()) {
        std::cerr << "Failed to receive SDP answer from server!" << std::endl;
        return;
      }

      // Log the server response for debugging.
      std::cout << "Server Response: " << serverResponse << std::endl;

      // Set the received SDP answer as the remote description.
      try {
        json responseJson = json::parse(serverResponse);
        std::string sdp_answer = responseJson["sdp"];
        std::cout << "Received SDP Answer:\n" << sdp_answer << std::endl;
        pc->setRemoteDescription(rtc::Description(sdp_answer, "answer"));

        std::cout << "Connection established!" << std::endl;
      } catch (const json::parse_error &e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
      }
    }
  });

  // Trigger ICE gathering.
  pc->setLocalDescription();

  // Create a dummy frame send loop (for testing).
  cv::Mat dummyFrame(480, 640, CV_8UC3, cv::Scalar(0, 255, 0));
  while (true) {
    // Wait till connection is established.
    if (connectionEstablished) {
      // Write dummy frame to disk.
      cv::imwrite("dummy_frame.jpg", dummyFrame);

      // Use ffmpeg to encode the dummy frame to H.264 (Annex B format).
      std::string cmd =
          "ffmpeg -y -i dummy_frame.jpg -c:v libx264 -x264-params "
          "keyint=1:scenecut=0:intra-refresh=0:repeat-headers=1 -tune "
          "zerolatency  -f h264 -";
      FILE *pipe = popen(cmd.c_str(), "r");
      if (!pipe) {
        std::cerr << "[C++] Failed to run ffmpeg!\n";
        break;
      }

      // Read the encoded frame from the pipe and send it.
      std::vector<uint8_t> buffer;
      uint8_t byte;
      while (fread(&byte, 1, 1, pipe) == 1) {
        buffer.push_back(byte);
      }
      pclose(pipe);
      if (!buffer.empty()) {
        track->send(reinterpret_cast<const std::byte *>(buffer.data()),
                    buffer.size());
        std::cout << "[C++] Sent original frame. size=" << buffer.size()
                  << std::endl;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }

  return 0;
}
