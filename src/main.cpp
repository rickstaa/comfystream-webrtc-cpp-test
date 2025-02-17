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
  "12": {
    "inputs": { "image": "sampled_frame.jpg", "upload": "image" },
    "class_type": "LoadImage",
    "_meta": { "title": "Load Image" }
  },
  "13": {
    "inputs": { "images": ["12", 0] },
    "class_type": "PreviewImage",
    "_meta": { "title": "Preview Image" }
  }
})";

/**
 * Send the SDP offer to the AI WebRTC Server.
 */
std::string sendSDPToServer(const std::string &sdp) {
  httplib::Client cli(
      "http://0.0.0.0:8889"); // or 127.0.0.1, depending on setup

  json request;
  request["offer"] = {{"sdp", sdp}, {"type", "offer"}};
  json prompt = json::parse(defaultPipelinePrompt);
  request["prompt"] = prompt;

  auto res = cli.Post("/offer", request.dump(), "application/json");
  return (res && res->status == 200) ? res->body : "";
}

/**
 * Dummy decode function. In a real application, you'd implement
 * an actual H.264 decoder (e.g., using FFmpeg libraries) to convert
 * raw H.264 data into a cv::Mat. Here, it just returns an empty Mat.
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

  // 1. Create PeerConnection
  rtc::Configuration config;
  config.iceServers.emplace_back("stun:stun.l.google.com:19302");
  auto pc = std::make_shared<rtc::PeerConnection>(config);

  // 2. Add video channel (SendRecv)
  const rtc::SSRC ssrc = 42;
  const uint8_t payloadType = 96;
  rtc::Description::Video media("video", rtc::Description::Direction::SendRecv);

  // Include typical H.264 parameters
  media.addH264Codec(
      payloadType,
      "packetization-mode=1;profile-level-id=42e01f;level-asymmetry-allowed=1");
  media.addSSRC(ssrc, "video-track");
  auto track = pc->addTrack(media);
  if (!track) {
    std::cerr << "[C++] Failed to add track!" << std::endl;
    return -1;
  }
  track->onFrame([](rtc::binary encodedData, rtc::FrameInfo info) {
    std::cout << "[C++] Received frame, size=" << encodedData.size() << std::endl;
  
    // Dummy decode
    cv::Mat frame = decodeH264ToMat(reinterpret_cast<const uint8_t *>(encodedData.data()), encodedData.size());
    if (!frame.empty()) {
      cv::imshow("Received Video", frame);
      cv::waitKey(1);
    }
  });

  // 3. Set up H.264 RTP packetizer for *outgoing* frames
  auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
      ssrc, "video-send", payloadType, 90000);
  auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
      rtc::NalUnit::Separator::LongStartSequence, rtpConfig, 1200);
  track->setMediaHandler(packetizer);

  bool connectionEstablished = false;

  // PeerConnection event listeners
  pc->onLocalDescription([&pc](rtc::Description sdp) {
    std::cout << "[Local Description] " << std::string(sdp) << std::endl;
  });

  pc->onLocalCandidate([](rtc::Candidate candidate) {
    std::cout << "[Local Candidate] " << candidate << std::endl;
  });

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

  // 4. Inbound track callback (AI-processed frames from Python)
  pc->onTrack([](std::shared_ptr<rtc::Track> inboundTrack) {
    std::cout << "[C++] Received remote track with mid: " << inboundTrack->mid()
              << std::endl;

    inboundTrack->onFrame([](rtc::binary encodedData, rtc::FrameInfo info) {
      // encodedData is H.264 in Annex B format
      std::cout << "[C++] Received processed frame, size=" << encodedData.size()
                << std::endl;

      // Dummy decode
      cv::Mat frame =
          decodeH264ToMat(reinterpret_cast<const uint8_t *>(encodedData.data()),
                          encodedData.size());
      if (!frame.empty()) {
        cv::imshow("Processed Video", frame);
        cv::waitKey(1);
      }
    });
  });

  // 5. Non-Trickle ICE (onGatheringStateChange)
  pc->onGatheringStateChange([&pc](rtc::PeerConnection::GatheringState state) {
    std::cout << "[C++] Gathering State: " << state << std::endl;
    if (state == rtc::PeerConnection::GatheringState::Complete) {
      std::cout << "[C++] All ICE candidates gathered." << std::endl;
      auto desc = pc->localDescription();
      if (!desc) {
        std::cerr << "[C++] No local description!" << std::endl;
        return;
      }
      std::string serverResponse =
          sendSDPToServer(static_cast<std::string>(*desc));
      if (serverResponse.empty()) {
        std::cerr << "Failed to receive SDP answer from server!" << std::endl;
        return;
      }

      json responseJson = json::parse(serverResponse);
      std::string sdp_answer = responseJson["sdp"];
      std::cout << "Received SDP Answer:\n" << sdp_answer << std::endl;

      pc->setRemoteDescription(rtc::Description(sdp_answer, "answer"));
      std::cout << "Connection established!" << std::endl;
    }
  });

  // Create Local Description (Offer)
  pc->setLocalDescription();

  // Build a dummy green image
  cv::Mat dummyFrame(480, 640, CV_8UC3, cv::Scalar(0, 255, 0));

  // Main loop: encode + send frames
  while (true) {
    if (connectionEstablished) {
      // 1) Write the dummy image
      cv::imwrite("dummy_frame.jpg", dummyFrame);

      // 2) Use ffmpeg to encode it to H.264 Annex B
      // std::string cmd = "ffmpeg -y -i dummy_frame.jpg -c:v libx264
      // -x264-params keyint=1 -f h264 -";
      std::string cmd =
          "ffmpeg -y -i dummy_frame.jpg -c:v libx264 -x264-params "
          "keyint=1:scenecut=0:intra-refresh=0:repeat-headers=1 -tune "
          "zerolatency  -f h264 -";
      FILE *pipe = popen(cmd.c_str(), "r");
      if (!pipe) {
        std::cerr << "[C++] Failed to run ffmpeg!\n";
        break;
      }

      std::vector<uint8_t> buffer;
      uint8_t byte;
      while (fread(&byte, 1, 1, pipe) == 1) {
        buffer.push_back(byte);
      }
      pclose(pipe);

      if (!buffer.empty()) {
        // Send via the "track" we created above
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
