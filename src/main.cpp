#include <iostream>
#include <rtc/rtc.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <opencv2/opencv.hpp>

extern "C"
{
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
    return (res && res->status == 200) ? res->body : "";
}

AVCodecContext *initFFmpegEncoder(int width, int height)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
    {
        std::cerr << "Codec not found" << std::endl;
        return nullptr;
    }

    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c)
    {
        std::cerr << "Could not allocate video codec context" << std::endl;
        return nullptr;
    }

    c->bit_rate = 500000;
    c->width = width;
    c->height = height;
    c->time_base = {1, 30};
    c->framerate = {30, 1};
    c->gop_size = 30; // ✅ Force keyframe every 30 frames
    c->max_b_frames = 0;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    c->codec_tag = 0;

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
    av_dict_set(&opts, "profile", "baseline", 0);
    av_dict_set(&opts, "packetization_mode", "1", 0); // ✅ Ensures fragmented NALUs
    av_dict_set(&opts, "keyint_min", "30", 0);
    av_dict_set(&opts, "sc_threshold", "0", 0);
    av_dict_set(&opts, "refs", "1", 0);
    av_dict_set(&opts, "force_key_frames", "expr:gte(t,n_forced*2)", 0);

    if (avcodec_open2(c, codec, &opts) < 0)
    {
        std::cerr << "Could not open codec" << std::endl;
        return nullptr;
    }

    av_dict_free(&opts);
    return c;
}

bool encodeFrame(AVCodecContext *c, AVFrame *frame, AVPacket *pkt, std::vector<uint8_t> &encodedData)
{
    frame->pict_type = AV_PICTURE_TYPE_I; // ✅ Use AV_PICTURE_TYPE_I (fix)
    frame->flags |= AV_FRAME_FLAG_KEY;    // ✅ Modern replacement for `key_frame`

    int ret = avcodec_send_frame(c, frame);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Error sending a frame for encoding: " << errbuf << std::endl;
        return false;
    }

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        else if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error during encoding: " << errbuf << std::endl;
            return false;
        }

        if (pkt->size < 500)
        {
            std::cerr << "[C++] Warning: Small H.264 packet, size = " << pkt->size << " bytes" << std::endl;
        }
        else
        {
            encodedData.insert(encodedData.end(), pkt->data, pkt->data + pkt->size);
            std::cout << "[DEBUG] Encoded frame size: " << pkt->size << " bytes" << std::endl;
        }

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
    const rtc::SSRC ssrc = 42;
    rtc::Description::Video media("video", rtc::Description::Direction::SendRecv);
    media.addH264Codec(102);
    media.addSSRC(ssrc, "video-send");
    auto track = pc->addTrack(media);
    if (!track)
        return -1;

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
                  { trackOpen = true; });

    while (!trackOpen)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int width = 640, height = 480;
    AVCodecContext *codecContext = initFFmpegEncoder(width, height);
    if (!codecContext)
        return -1;

    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return -1;

    frame->format = codecContext->pix_fmt;
    frame->width = codecContext->width;
    frame->height = codecContext->height;
    av_image_alloc(frame->data, frame->linesize, width, height, codecContext->pix_fmt, 32);

    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        return -1;

    cv::Mat dummyFrame(height, width, CV_8UC3, cv::Scalar(0, 255, 0));
    SwsContext *swsContext = sws_getContext(width, height, AV_PIX_FMT_BGR24, width, height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext)
        return -1;

    while (true)
    {
        const uint8_t *inData[1] = {dummyFrame.data};
        int inLinesize[1] = {static_cast<int>(dummyFrame.step[0])};
        sws_scale(swsContext, inData, inLinesize, 0, height, frame->data, frame->linesize);
        frame->pts++;

        std::vector<uint8_t> encodedData;
        if (!encodeFrame(codecContext, frame, pkt, encodedData))
            continue;

        for (size_t i = 0; i < encodedData.size(); i += 1200)
        {
            size_t packetSize = std::min(1200UL, encodedData.size() - i);
            std::vector<uint8_t> packet(encodedData.begin() + i, encodedData.begin() + i + packetSize);
            std::vector<std::byte> bytePacket(reinterpret_cast<std::byte *>(packet.data()),
                                              reinterpret_cast<std::byte *>(packet.data() + packet.size()));
            std::cout << "[C++] Sent RTP packet - Size: " << packetSize << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}
