#include <iostream>
#include <rtc/rtc.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

using json = nlohmann::json;

// Default media pass-through pipeline prompt for the AI WebRTC Server.
const std::string defaultPipelinePrompt = R"({ "12": { "inputs": { "image": "sampled_frame.jpg", "upload": "image" }, "class_type": "LoadImage", "_meta": { "title": "Load Image" } }, "13": { "inputs": { "images": ["12", 0] }, "class_type": "PreviewImage", "_meta": { "title": "Preview Image" } } })";

/**
 * Send the SDP offer to the AI WebRTC Server.
 * @param sdp The SDP offer to send.
 */
std::string sendSDPToServer(const std::string &sdp)
{
    httplib::Client cli("http://0.0.0.0:8888"); // Local Comfystream server.

    json request;
    request["offer"] = {{"sdp", sdp}, {"type", "offer"}};
    json prompt = json::parse(defaultPipelinePrompt);
    request["prompt"] = prompt;

    auto res = cli.Post("/offer", request.dump(), "application/json");
    if (res && res->status == 200)
    {
        return res->body;
    }
    return "";
}

/**
 * Initialize FFmpeg encoder.
 */
AVCodecContext* initFFmpegEncoder(int width, int height)
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "Codec not found" << std::endl;
        return nullptr;
    }

    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c) {
        std::cerr << "Could not allocate video codec context" << std::endl;
        return nullptr;
    }

    c->bit_rate = 400000;
    c->width = width;
    c->height = height;
    c->time_base = {1, 30};
    c->framerate = {30, 1};
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avcodec_open2(c, codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return nullptr;
    }

    return c;
}

/**
 * Encode a frame using FFmpeg.
 */
bool encodeFrame(AVCodecContext* c, AVFrame* frame, AVPacket* pkt, std::vector<std::byte>& encodedData)
{
    int ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        std::cerr << "Error sending a frame for encoding" << std::endl;
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return true;
        } else if (ret < 0) {
            std::cerr << "Error during encoding" << std::endl;
            return false;
        }

        encodedData.insert(encodedData.end(), reinterpret_cast<std::byte*>(pkt->data), reinterpret_cast<std::byte*>(pkt->data + pkt->size));
        av_packet_unref(pkt);
    }

    return true;
}

int main()
{
    rtc::InitLogger(rtc::LogLevel::Info);
    std::cout << "Starting AI WebRTC Client..." << std::endl;

    // Step 1: Create PeerConnection.
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    auto pc = std::make_shared<rtc::PeerConnection>(config);

    // ### Trickling ICE - Not Working ###
    // // Handle the local description when it's generated and send it to the server (trickle ICE)
    // NOTE: Doesn't seem to work with ComfyStream server.
    // pc->onLocalDescription([&pc](rtc::Description sdp)
    //                        {
    //     std::cout << "Generated SDP Offer:\n" << std::string(sdp) << std::endl;

    //     std::string server_response = sendSDPToServer(std::string(sdp));

    //     if (server_response.empty()) {
    //         std::cerr << "Failed to receive SDP answer from server!" << std::endl;
    //         return;
    //     }

    //     json response_json = json::parse(server_response);
    //     std::string sdp_answer = response_json["sdp"];

    //     std::cout << "Received SDP Answer:\n" << sdp_answer << std::endl;

    //     pc->setRemoteDescription(rtc::Description(sdp_answer, sdp.type()));
    //     std::cout << "Connection established!" << std::endl; });
    // pc->onGatheringStateChange([pc](rtc::PeerConnection::GatheringState state)
    //                         { std::cout << "[Gathering State Change] New state: " << state << std::endl; });
    // ### Trickling ICE - Not Working ###

    // ### Non-Trickling ICE - Working ###
    // Handle the local description when it's generated and send it to the server.
    pc->onLocalDescription([&pc](rtc::Description sdp)
                           { std::cout << "Generated SDP Offer:\n"
                                       << std::string(sdp) << std::endl; });

    // Step 4: Send the SDP offer to the server and handle the SDP answer.
    pc->onGatheringStateChange([&pc](rtc::PeerConnection::GatheringState state)
                               {
        std::cout << "Gathering State: " << state << std::endl;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            std::cout << "All ICE candidates have been gathered." << std::endl;
            auto description = pc->localDescription();
            if (!description) {
                std::cerr << "Failed to get local description!" << std::endl;
                return;
            }
            std::string server_response = sendSDPToServer(static_cast<std::string>(*description));

            if (server_response.empty()) {
                std::cerr << "Failed to receive SDP answer from server!" << std::endl;
                return;
            }

            json response_json = json::parse(server_response);
            std::string sdp_answer = response_json["sdp"];

            std::cout << "Received SDP Answer:\n" << sdp_answer << std::endl;

            pc->setRemoteDescription(rtc::Description(sdp_answer, "answer"));
            std::cout << "Connection established!" << std::endl;
        } });
     // ### Non-Trickling ICE - Working ###

    // Add other event listeners.
    pc->onLocalCandidate([](rtc::Candidate candidate)
                         { std::cout << "[Local Candidate] " << candidate << std::endl; });
    pc->onStateChange([](rtc::PeerConnection::State state)
                      { std::cout << "[PeerConnection State Change] New state: " << state << std::endl; });
    pc->onIceStateChange([](rtc::PeerConnection::IceState state)
                         { std::cout << "[ICE State Change] New state: " << state << std::endl; });
    pc->onSignalingStateChange([](rtc::PeerConnection::SignalingState state)
                               { std::cout << "[Signaling State Change] New state: " << state << std::endl; });
    pc->onTrack([](std::shared_ptr<rtc::Track> track)
                {
        std::cout << "Track added with mid: " << track->mid() << std::endl;
        track->onFrame([](rtc::binary data, rtc::FrameInfo frame) {
            std::cout << "Received frame of size: " << data.size() << std::endl;
        }); });

    // Step 2: Add video channel to the PeerConnection.
    // rtc::Description::Video media("video", rtc::Description::Direction::SendRecv);
    // media.addH264Codec(102); // Match AI server's codec
    // media.addSSRC(42, "video-send");
    // auto track = pc->addTrack(media);
    const rtc::SSRC ssrc = 42;
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96); // Must match the payload type of the external h264 RTP stream
    media.addSSRC(ssrc, "video-send");
    auto track = pc->addTrack(media);
    if (!track)
    {
        std::cerr << "Failed to add video track" << std::endl;
        return -1;
    }

    // Step 3: Add a data channel to the PeerConnection.
    auto dc = pc->createDataChannel("control");
    if (!dc)
    {
        std::cerr << "Failed to create data channel" << std::endl;
        return -1;
    }
    dc->onOpen([&]()
               { std::cout << "[DataChannel open: " << dc->label() << "]" << std::endl; });
    dc->onClosed([&]()
                 { std::cout << "[DataChannel closed: " << dc->label() << "]" << std::endl; });
    dc->onError([](std::string error)
                { std::cerr << "[DataChannel error: " << error << "]" << std::endl; });
    dc->onMessage([](auto data)
                  {
        if (std::holds_alternative<std::string>(data)) {
            std::cout << "[Received: " << std::get<std::string>(data) << "]" << std::endl;
        } });

    // Step 5: Wait until the track is open before sending dummy data.
    std::atomic<bool> trackOpen{false};
    track->onOpen([&trackOpen]()
                  {
        trackOpen = true;
        std::cout << "Track is open" << std::endl; });

    // Wait until the track is open.
    while (!trackOpen)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ### Dummy Data
    // // Step 6: Send dummy video frames to verify the connection.
    // cv::Mat dummyFrame(480, 640, CV_8UC3, cv::Scalar(0, 255, 0)); // Green frame
    // std::vector<uchar> buf;
    // cv::imencode(".jpg", dummyFrame, buf);
    // std::vector<std::byte> dummyData(buf.size());
    // std::transform(buf.begin(), buf.end(), dummyData.begin(), [](uchar c) {
    //     return std::byte(c);
    // });

    // while (true) {
    //     track->send(dummyData.data(), dummyData.size());
    //     std::cout << "Sent dummy frame of size: " << dummyData.size() << std::endl;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Send every second
    // }
    // ### Dummy Data

    // ### Webcam Data - Not Working ###
    // Step 6: Capture and send camera data.
    cv::VideoCapture cap(0); // Open the default camera
    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera" << std::endl;
        return -1;
    }

    int width = 640;
    int height = 480;
    AVCodecContext* codecContext = initFFmpegEncoder(width, height);
    if (!codecContext) {
        return -1;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Could not allocate video frame" << std::endl;
        return -1;
    }
    frame->format = codecContext->pix_fmt;
    frame->width = codecContext->width;
    frame->height = codecContext->height;

    int ret = av_image_alloc(frame->data, frame->linesize, codecContext->width, codecContext->height, codecContext->pix_fmt, 32);
    if (ret < 0) {
        std::cerr << "Could not allocate raw picture buffer" << std::endl;
        return -1;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Could not allocate AVPacket" << std::endl;
        return -1;
    }

    SwsContext* swsContext = sws_getContext(width, height, AV_PIX_FMT_BGR24, width, height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext) {
        std::cerr << "Could not initialize the conversion context" << std::endl;
        return -1;
    }

    cv::Mat bgrFrame;
    while (true) {
        cap >> bgrFrame; // Capture a new frame from the camera
        if (bgrFrame.empty()) {
            std::cerr << "Failed to capture frame" << std::endl;
            continue;
        }

        // Convert the frame to YUV420P
        const uint8_t* inData[1] = { bgrFrame.data };
        int inLinesize[1] = { static_cast<int>(bgrFrame.step[0]) };
        sws_scale(swsContext, inData, inLinesize, 0, height, frame->data, frame->linesize);

        // Encode the frame
        std::vector<std::byte> encodedData;
        if (!encodeFrame(codecContext, frame, pkt, encodedData)) {
            std::cerr << "Failed to encode frame" << std::endl;
            continue;
        }

        // Send encoded frame as RTP packets
        size_t maxPacketSize = 1200; // Maximum RTP packet size
        for (size_t i = 0; i < encodedData.size(); i += maxPacketSize) {
            size_t packetSize = std::min(maxPacketSize, encodedData.size() - i);
            track->send(encodedData.data() + i, packetSize);
        }

        std::cout << "Sent frame of size: " << encodedData.size() << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // Send at ~30 FPS
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codecContext);
    sws_freeContext(swsContext);
    // ### Webcam Data - Not Working ###
}
