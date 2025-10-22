#include "webrtc_publisher.h"
#include <iostream>
#include <functional>
#include <chrono>
#include <future>
#include <cstring>

#include <random>
#include <algorithm>
#include "rtc/rtc.hpp"

std::string localId;
std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>> peerConnectionMap;
#include <memory>

template <class T>
std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr)
{
    return std::weak_ptr<T>(ptr);
}

std::string randomId(size_t length)
{
    using std::chrono::high_resolution_clock;
    static thread_local std::mt19937 rng(
        static_cast<unsigned int>(high_resolution_clock::now().time_since_epoch().count()));
    static const std::string characters(
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    std::string id(length, '0');
    std::uniform_int_distribution<int> uniform(0, int(characters.size() - 1));
    std::generate(id.begin(), id.end(), [&]()
                  { return characters.at(uniform(rng)); });
    return id;
}
// Create and setup a PeerConnection
shared_ptr<rtc::PeerConnection> createPeerConnection(const rtc::Configuration &config,
                                                     weak_ptr<rtc::WebSocket> wws, std::string id,
                                                     VideoCapturer &video_capturer)
{
    auto pc = std::make_shared<rtc::PeerConnection>(config);

    pc->onStateChange([](rtc::PeerConnection::State state)
                      {
    std::cout << "PeerConnection State: " << state << std::endl;
    switch (state) {
        case rtc::PeerConnection::State::New:
            std::cout << " - New" << std::endl;
            break;
        case rtc::PeerConnection::State::Connecting:
            std::cout << " - Connecting" << std::endl;
            break;
        case rtc::PeerConnection::State::Connected:
            std::cout << " - Connected" << std::endl;
            break;
        case rtc::PeerConnection::State::Disconnected:
            std::cout << " - Disconnected" << std::endl;
            break;
        case rtc::PeerConnection::State::Failed:
            std::cout << " - Failed" << std::endl;
            break;
        case rtc::PeerConnection::State::Closed:
            std::cout << " - Closed" << std::endl;
            break;
    } });

    pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state)
                               { std::cout << "Gathering State: " << state << std::endl; });

    pc->onLocalDescription([wws, id](rtc::Description description)
                           {
        std::cout << "send answer, type: " << description.typeString() << std::endl;
        json message = {{"id", id},
                        {"type", description.typeString()},
                        {"description", std::string(description)}};

        if (auto ws = wws.lock())
            ws->send(message.dump()); });

    pc->onLocalCandidate([wws, id](rtc::Candidate candidate)
                         {
        json message = {{"id", id},
                        {"type", "candidate"},
                        {"candidate", std::string(candidate)},
                        {"mid", candidate.mid()}};

        if (auto ws = wws.lock())
            ws->send(message.dump()); });

    // Create video track and add to connection
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96); // Add H.264 codec with payload type 96

    // 设置 H.264 参数（可选）
    // media.addH264Codec(96, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");

    // 生成唯一的 SSRC 和 CNAME
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(1, 0xFFFFFFFF);
    uint32_t ssrc = dis(gen);
    std::string cname = "video_" + std::to_string(ssrc);
    std::string msid = "stream_" + id;

    media.addSSRC(ssrc, cname, msid, cname);

    auto track = pc->addTrack(media);

    // 设置 H.264 RTP 媒体处理器
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        ssrc,
        cname,
        96, // H.264 payload type
        rtc::H264RtpPacketizer::ClockRate);

    // 创建 H.264 RTP 打包器
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::StartSequence, // 使用长起始序列
        rtpConfig);

    // 添加 RTCP SR (Sender Report) 报告器
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);

    // 添加 RTCP NACK 响应器
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);

    // 设置轨道的媒体处理器
    track->setMediaHandler(packetizer);

    pc->onTrack([id](std::shared_ptr<rtc::Track> track)
                { std::cout << "Track from " << id << " received with mid \"" << track->mid() << "\""
                            << std::endl; });

    track->onOpen([id, track, &video_capturer]()
                  {
        std::cout << "Track to " << id << " is now open" << std::endl;
        video_capturer.resume_capture();
        // Keep track of start time for timestamp calculation
        uint64_t start_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        video_capturer.set_track_callback([track, start_time](const std::byte *data, size_t size)
                                          {
    if (track && track->isOpen())
    {
        try 
        {
            // Calculate timestamp in microseconds since start
            uint64_t current_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            uint64_t timestamp_us = current_time - start_time;
            // Send frame with timestamp like in the streamer example
            track->sendFrame(reinterpret_cast<const std::byte*>(data), size, std::chrono::duration<double, std::micro>(timestamp_us));
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to send video data: " << e.what() << std::endl;
        }
    }
    else
    {
        std::cout << "Track is not open" << std::endl;
    } }); });

    track->onClosed([id, &video_capturer]()
                    {
                        std::cout << "Track to " << id << " closed" << std::endl;
                        video_capturer.set_track_callback(nullptr);
                        video_capturer.pause_capture(); });

    peerConnectionMap.emplace(id, pc);
    return pc;
}

WebRTCPublisher::WebRTCPublisher(const std::string &client_id, Cmdline params, const std::string &input_device)
    : client_id_(client_id), params_(params), video_capturer_(input_device, params.debug())
{
    rtc::InitLogger(rtc::LogLevel::Info);
    localId = client_id;
}

void WebRTCPublisher::start()
{
    // 配置WebRTC
    rtc::Configuration config;

    if (!params_.noStun())
    {
        std::string stunServer = params_.stunServer();
        int stunPort = params_.stunPort();

        std::string stunUrl = "stun:";
        if (stunServer.substr(0, 5) == "stun:")
        {
            stunUrl = stunServer;
        }
        else
        {
            stunUrl += stunServer;
        }

        stunUrl += ":" + std::to_string(stunPort);
        std::cout << "STUN server is " << stunUrl << std::endl;
        config.iceServers.emplace_back(stunUrl);
    }
    else
    {
        std::cout << "No STUN server is configured. Only local hosts and public IP addresses supported." << std::endl;
    }

    // 添加 TURN 服务器配置支持
    if (!params_.turnServer().empty())
    {
        std::string turnServer = params_.turnServer();
        std::string turnUser = params_.turnUser();
        std::string turnPass = params_.turnPass();
        int turnPort = params_.turnPort();

        std::cout << "TURN server is " << turnServer << ":" << turnPort << std::endl;

        // TURN 服务器 - 使用带参数的构造函数
        config.iceServers.push_back(rtc::IceServer(
            turnServer,                        // hostname
            turnPort,                          // port
            turnUser,                          // username
            turnPass,                          // password
            rtc::IceServer::RelayType::TurnUdp // relay type
            ));
    }

    if (params_.udpMux())
    {
        std::cout << "ICE UDP mux enabled" << std::endl;
        config.enableIceUdpMux = true;
    }

    // 启动视频捕获
    if (!video_capturer_.start())
    {
        throw std::runtime_error("Failed to start video capture");
    }
    else
    {
        std::cout << "Video capture thread started" << std::endl;
    }

    ws_ = std::make_shared<rtc::WebSocket>();

    std::promise<void> wsPromise;
    auto wsFuture = wsPromise.get_future();

    ws_->onOpen([&wsPromise]()
                {
        std::cout << "WebSocket connected, signaling ready" << std::endl;
        wsPromise.set_value(); });

    ws_->onError([&wsPromise](std::string s)
                 {
        std::cout << "WebSocket error" << std::endl;
        wsPromise.set_exception(std::make_exception_ptr(std::runtime_error(s))); });

    ws_->onClosed([]()
                  { std::cout << "WebSocket closed" << std::endl; });

    ws_->onMessage([config, wws = make_weak_ptr(ws_), this](auto data)
                   {
        // data holds either std::string or rtc::binary
        if (!std::holds_alternative<std::string>(data))
            return;

        json message = json::parse(std::get<std::string>(data));

        auto it = message.find("id");
        if (it == message.end())
            return;

        auto id = it->get<std::string>();

        it = message.find("type");
        if (it == message.end())
            return;

        auto type = it->get<std::string>();

        shared_ptr<rtc::PeerConnection> pc;
        if (auto jt = peerConnectionMap.find(id); jt != peerConnectionMap.end()) {
            pc = jt->second;
        } else if (type == "offer") {
            std::cout << "Answering to " + id << std::endl;
            pc = createPeerConnection(config, wws, id, video_capturer_);
            
        } else {
            return;
        }

        if (type == "offer" || type == "answer") {
            auto sdp = message["description"].get<std::string>();
            pc->setRemoteDescription(rtc::Description(sdp, type));
        } else if (type == "candidate") {
            auto sdp = message["candidate"].get<std::string>();
            auto mid = message["mid"].get<std::string>();
            pc->addRemoteCandidate(rtc::Candidate(sdp, mid));
    } });

    std::string webSocketServer = params_.webSocketServer();
    int webSocketPort = params_.webSocketPort();

    const std::string wsPrefix = webSocketServer.find("://") == std::string::npos ? "ws://" : "";
    const std::string url = wsPrefix + webSocketServer + ":" +
                            std::to_string(webSocketPort) + "/" + client_id_;

    std::cout << "WebSocket URL is " << url << std::endl;
    ws_->open(url);

    std::cout << "Waiting for signaling to be connected..." << std::endl;
    wsFuture.get();

    std::cout << "Publisher is ready. Client ID: " << client_id_ << std::endl;
    std::cout << "Viewers can connect using this client ID." << std::endl;
}

void WebRTCPublisher::stop()
{
    video_capturer_.stop();
    peerConnectionMap.clear();

    if (ws_)
    {
        ws_->close();
    }
}