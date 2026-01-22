#include "webrtc_publisher.h"
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>

#include "audio_player.h"
#include "opus_encoder.h"
#include "rtc/rtc.hpp"
#include <algorithm>
#include <random>

std::string localId;
std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>>
    peerConnectionMap;
#include <memory>

template <class T> std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) {
  return std::weak_ptr<T>(ptr);
}

std::string randomId(size_t length) {
  using std::chrono::high_resolution_clock;
  static thread_local std::mt19937 rng(static_cast<unsigned int>(
      high_resolution_clock::now().time_since_epoch().count()));
  static const std::string characters(
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
  std::string id(length, '0');
  std::uniform_int_distribution<int> uniform(0, int(characters.size() - 1));
  std::generate(id.begin(), id.end(),
                [&]() { return characters.at(uniform(rng)); });
  return id;
}
// Create and setup a PeerConnection
shared_ptr<rtc::PeerConnection>
createPeerConnection(const rtc::Configuration &config,
                     weak_ptr<rtc::WebSocket> wws, std::string id,
                     VideoCapturer *video_capturer,
                     AudioCapturer *audio_capturer, AudioPlayer *audio_player) {
  auto pc = std::make_shared<rtc::PeerConnection>(config);

  pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
    std::cout << "Gathering State: " << state << std::endl;
  });

  pc->onLocalDescription([wws, id](rtc::Description description) {
    std::cout << "send answer, type: " << description.typeString() << std::endl;
    json message = {{"id", id},
                    {"type", description.typeString()},
                    {"description", std::string(description)}};

    if (auto ws = wws.lock())
      ws->send(message.dump());
  });

  pc->onLocalCandidate([wws, id](rtc::Candidate candidate) {
    json message = {{"id", id},
                    {"type", "candidate"},
                    {"candidate", std::string(candidate)},
                    {"mid", candidate.mid()}};

    if (auto ws = wws.lock())
      ws->send(message.dump());
  });

  // 生成唯一的 SSRC 和 CNAME
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint32_t> dis(1, 0xFFFFFFFF);
  shared_ptr<rtc::Track> video_track = nullptr;
  shared_ptr<rtc::Track> audio_track = nullptr;

  if (video_capturer != nullptr && video_capturer->is_running()) {
    // Create video track and add to connection
    rtc::Description::Video media("video",
                                  rtc::Description::Direction::SendOnly);
    media.addH264Codec(96); // Add H.264 codec with payload type 96
    // 设置 H.264 参数（可选）
    // media.addH264Codec(96,
    // "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
    uint32_t video_ssrc = dis(gen);
    std::string cname = "video_" + std::to_string(video_ssrc);
    std::string msid = "stream_" + id;

    media.addSSRC(video_ssrc, cname, msid, cname);

    video_track = pc->addTrack(media);

    // 设置 H.264 RTP 媒体处理器
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        video_ssrc, cname,
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
    video_track->setMediaHandler(packetizer);

    video_track->onOpen([id, video_track, video_capturer]() {
      std::cout << "Video track to " << id << " is now open" << std::endl;
      // Keep track of start time for timestamp calculation
      uint64_t start_time =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      video_capturer->set_track_callback(
          [video_track, start_time](const std::byte *data, size_t size) {
            if (video_track && video_track->isOpen()) {
              try {
                // Calculate timestamp in microseconds since start
                uint64_t current_time =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
                uint64_t timestamp_us = current_time - start_time;
                // Send frame with timestamp like in the streamer example
                video_track->sendFrame(
                    reinterpret_cast<const std::byte *>(data), size,
                    std::chrono::duration<double, std::micro>(timestamp_us));
              } catch (const std::exception &e) {
                std::cerr << "Failed to send video data: " << e.what()
                          << std::endl;
              }
            } else {
              std::cout << "Video track is not open" << std::endl;
            }
          });
      video_capturer->resume_capture();
    });

    video_track->onClosed([id, video_capturer]() {
      std::cout << "Video Track to " << id << " closed" << std::endl;
      // 检查capturer是否仍然有效
      if (video_capturer) {
        video_capturer->pause_capture();
        video_capturer->set_track_callback(nullptr);
      }
    });
  }

  if (audio_capturer != nullptr && audio_capturer->is_running()) {
    rtc::Description::Audio audio_media("audio",
                                        rtc::Description::Direction::SendRecv);
    audio_media.addOpusCodec(111); // Add Opus codec with payload type 111
    uint32_t audio_ssrc = dis(gen);
    std::string audio_cname = "audio_" + std::to_string(audio_ssrc);
    std::string audio_msid = "stream_" + id;

    audio_media.addSSRC(audio_ssrc, audio_cname, audio_msid, audio_cname);

    audio_track = pc->addTrack(audio_media);

    // 设置 Opus RTP 媒体处理器
    auto audio_rtpConfig =
        std::make_shared<rtc::RtpPacketizationConfig>(audio_ssrc, audio_cname,
                                                      111, // Opus payload type
                                                      48000); // Opus clock rate

    // 创建 Opus RTP 打包器
    auto audio_packetizer =
        std::make_shared<rtc::OpusRtpPacketizer>(audio_rtpConfig);

    // 添加 RTCP SR (Sender Report) 报告器
    auto audio_srReporter =
        std::make_shared<rtc::RtcpSrReporter>(audio_rtpConfig);
    audio_packetizer->addToChain(audio_srReporter);

    // 添加 RTCP NACK 响应器
    auto audio_nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    audio_packetizer->addToChain(audio_nackResponder);

    // 设置轨道的媒体处理器
    audio_track->setMediaHandler(audio_packetizer);

    audio_track->onOpen([id, audio_track, audio_capturer]() {
      std::cout << "Audio track to " << id << " is now open" << std::endl;
      // Keep track of start time for timestamp calculation
      uint64_t start_time =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      audio_capturer->set_track_callback(
          [audio_track, start_time](const std::byte *data, size_t size) {
            if (audio_track && audio_track->isOpen()) {
              try {
                // Calculate timestamp in microseconds since start
                uint64_t current_time =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
                uint64_t timestamp_us = current_time - start_time;
                // Send frame with timestamp like in the streamer example
                audio_track->sendFrame(
                    reinterpret_cast<const std::byte *>(data), size,
                    std::chrono::duration<double, std::micro>(timestamp_us));
              } catch (const std::exception &e) {
                std::cerr << "Failed to send audio data: " << e.what()
                          << std::endl;
              }
            } else {
              std::cout << "Audio track is not open" << std::endl;
            }
          });
      audio_capturer->resume_capture();
    });

    audio_track->onClosed([id, audio_capturer]() {
      std::cout << "Audio Track to " << id << " closed" << std::endl;
      // 检查capturer是否仍然有效
      if (audio_capturer) {
        audio_capturer->pause_capture();
        audio_capturer->set_track_callback(nullptr);
      }
    });
  }

  // 修复音频接收逻辑
  std::shared_ptr<rtc::Track> audioReceiver = nullptr;
  pc->onTrack([audio_player, audioReceiver](
                  const std::shared_ptr<rtc::Track> &track) mutable {
    if (track->description().type() == "audio") {
      audioReceiver = track;

      // Set up the receiving pipeline
      auto depacketizer = std::make_shared<rtc::OpusRtpDepacketizer>(48000);
      depacketizer->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
      audioReceiver->setMediaHandler(depacketizer);
      audioReceiver->onOpen([audioReceiver]() {
        std::cout << "Received Audio Track from " << audioReceiver->mid()
                  << " is now open" << std::endl;
      });
      audioReceiver->onClosed([audioReceiver]() {
        std::cout << "Audio Track from " << audioReceiver->mid() << " closed"
                  << std::endl;
      });
      audioReceiver->onFrame(
          [audio_player](const rtc::binary &data, const rtc::FrameInfo &info) {
            if (audio_player != nullptr) {
              audio_player->receiveAudioData(data, info);
            }
          });
    }
  });
  // 通道关闭时，停止音视频捕获
  pc->onStateChange(
      [video_capturer, audio_capturer](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Disconnected ||
            state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Closed) {
          std::cout << "PeerConnection closed, stopping captures!" << std::endl;
          // 检查capturer是否仍然有效
          if (video_capturer && video_capturer->is_running()) {
            video_capturer->set_track_callback(nullptr);
            video_capturer->pause_capture();
          }
          if (audio_capturer && audio_capturer->is_running()) {
            audio_capturer->set_track_callback(nullptr);
            audio_capturer->pause_capture();
          }
        }
        if (state == rtc::PeerConnection::State::Connected) {
          std::cout << "PeerConnection connected" << std::endl;
        }
      });
  // 记录 PeerConnection
  peerConnectionMap.emplace(id, pc);
  return pc;
}

WebRTCPublisher::WebRTCPublisher(const std::string &client_id, Cmdline params)
    : client_id_(client_id), params_(params) {
  rtc::InitLogger(rtc::LogLevel::Info);
  localId = client_id;
    size_t queue_size = params.framerate()*2;
  // Initialize video capturer
  if (!params.inputDevice().empty()) {
    video_capturer_ = new VideoCapturer(params.inputDevice(), params.debug(),
                                        params.resolution(), params.framerate(),
                                        params.videoFormat(), queue_size,
                                        queue_size, queue_size);
  } else {
    video_capturer_ = nullptr;
  }

  // Initialize audio capturer if audio device is specified
  if (!params.audioDevice().empty()) {
    AudioDeviceParams audio_params;
    audio_params.device = params.audioDevice();
    audio_params.channels = params.channels();
    audio_params.input_format = params.audioFormat();
    audio_params.sample_rate = params.sampleRate();
    audio_capturer_ =
        new AudioCapturer(audio_params, params.debug(), queue_size,
            queue_size, queue_size);
  } else {
    audio_capturer_ = nullptr;
  }

  // Initialize audio player with the specified playback device
  if (!params.speakerDevice().empty()) {
    AudioPlayerDeviceParams audio_params;
    audio_params.out_channels = params.outChannels();
    audio_params.out_device = params.speakerDevice();
    audio_params.out_sample_rate = params.outSampleRate();
    audio_params.volume = params.volume();
    audio_player_ = new AudioPlayer(audio_params);
  } else {
    audio_player_ = nullptr;
  }

  std::cout << "WebRTCPublisher init..." << std::endl;
}

void WebRTCPublisher::start() {
  // 配置WebRTC
  rtc::Configuration config;

  if (!params_.noStun()) {
    std::string stunServer = params_.stunServer();
    int stunPort = params_.stunPort();

    std::string stunUrl = "stun:";
    if (stunServer.substr(0, 5) == "stun:") {
      stunUrl = stunServer;
    } else {
      stunUrl += stunServer;
    }

    stunUrl += ":" + std::to_string(stunPort);
    std::cout << "STUN server is " << stunUrl << std::endl;
    config.iceServers.emplace_back(stunUrl);
  } else {
    std::cout << "No STUN server is configured. Only local hosts and public IP "
                 "addresses supported."
              << std::endl;
  }

  // 添加 TURN 服务器配置支持
  if (!params_.turnServer().empty()) {
    std::string turnServer = params_.turnServer();
    std::string turnUser = params_.turnUser();
    std::string turnPass = params_.turnPass();
    int turnPort = params_.turnPort();

    std::cout << "TURN server is " << turnServer << ":" << turnPort
              << std::endl;

    // TURN 服务器 - 使用带参数的构造函数
    config.iceServers.push_back(
        rtc::IceServer(turnServer,                        // hostname
                       turnPort,                          // port
                       turnUser,                          // username
                       turnPass,                          // password
                       rtc::IceServer::RelayType::TurnUdp // relay type
                       ));
  }

  if (params_.udpMux()) {
    std::cout << "ICE UDP mux enabled" << std::endl;
    config.enableIceUdpMux = true;
  }

  // 启动视频捕获
  if (video_capturer_ != nullptr) {
    if (!video_capturer_->start()) {
      throw std::runtime_error("Failed to start video capture");
    } else {
      std::cout << "Video capture thread started" << std::endl;
    }
  } else {
    std::cout << "No video device is specified" << std::endl;
  }

  // 启动音频捕获
  if (audio_capturer_ != nullptr) {
    if (!audio_capturer_->start()) {
      throw std::runtime_error("Failed to start audio capture");
    } else {
      std::cout << "Audio capture thread started" << std::endl;
    }
  } else {
    std::cout << "No audio device is specified" << std::endl;
  }

  // 启动音频播放器
  if (audio_player_ != nullptr) {
    audio_player_->start();
    std::cout << "Audio player started" << std::endl;
  }

  ws_ = std::make_shared<rtc::WebSocket>();

  std::promise<void> wsPromise;
  auto wsFuture = wsPromise.get_future();

  ws_->onOpen([&wsPromise]() {
    std::cout << "WebSocket connected, signaling ready" << std::endl;
    wsPromise.set_value();
  });

  ws_->onError([&wsPromise](std::string s) {
    std::cout << "WebSocket error" << std::endl;
    wsPromise.set_exception(std::make_exception_ptr(std::runtime_error(s)));
  });

  ws_->onClosed([]() {
    std::cout << "WebSocket closed" << std::endl;
    exit(5);
  });

  ws_->onMessage([config, wws = make_weak_ptr(ws_), this](auto data) {
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
      if (type == "offer") {
        std::cout << "Release old pc" << std::endl;
        peerConnectionMap[id]->close();
        peerConnectionMap.erase(id);
        std::cout << "Answering to " + id << std::endl;
        pc = createPeerConnection(config, wws, id, this->video_capturer_,
                                  this->audio_capturer_, this->audio_player_);

      } else {
        pc = jt->second;
      }
    } else if (type == "offer") {
      std::cout << "Answering to " + id << std::endl;
      pc = createPeerConnection(config, wws, id, this->video_capturer_,
                                this->audio_capturer_, this->audio_player_);

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
    }
  });

  std::string webSocketServer = params_.webSocketServer();
  int webSocketPort = params_.webSocketPort();

  const std::string wsPrefix =
      webSocketServer.find("://") == std::string::npos ? "ws://" : "";
  const std::string url = wsPrefix + webSocketServer + ":" +
                          std::to_string(webSocketPort) + "/" + client_id_;

  std::cout << "WebSocket URL is " << url << std::endl;
  ws_->open(url);

  std::cout << "Waiting for signaling to be connected..." << std::endl;
  wsFuture.get();

  std::cout << "Publisher is ready. Client ID: " << client_id_ << std::endl;
  std::cout << "Viewers can connect using this client ID." << std::endl;
}

void WebRTCPublisher::stop() {
  // 关闭所有PeerConnection以确保回调被清理
  for (auto &pair : peerConnectionMap) {
    if (pair.second) {
      pair.second->close();
    }
  }
  peerConnectionMap.clear();

  if (ws_) {
    ws_->close();
    ws_.reset();
  }

  // 等待一小段时间确保所有回调都已完成
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (video_capturer_ != nullptr) {
    video_capturer_->stop();
    delete video_capturer_;
    video_capturer_ = nullptr;
  }

  if (audio_capturer_ != nullptr) {
    audio_capturer_->stop();
    delete audio_capturer_;
    audio_capturer_ = nullptr;
  }

  if (audio_player_ != nullptr) {
    audio_player_->stop();
    delete audio_player_;
    audio_player_ = nullptr;
  }
}