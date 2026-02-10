#include "webrtc_publisher.h"
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>

#include "audio_player.h"
#include "opus_encoder.h"
#include "rtc/rtc.hpp"
#include <algorithm>
#include <random>
#include <nlohmann/json.hpp>

template <class T> weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) {
  return weak_ptr<T>(ptr);
}

// Helper function to generate a random ID
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

// 创建并设置 PeerConnection
shared_ptr<rtc::PeerConnection> WebRTCPublisher::createPeerConnection(
    const rtc::Configuration &config,
    weak_ptr<rtc::WebSocket> wws,
    const std::string &id) {
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

  if (video_capturer_ != nullptr && video_capturer_->is_running()) {
    // 获取当前视频编码器类型
    std::string video_codec = video_capturer_->get_video_codec();
    std::cout << "Using video codec: " << video_codec << std::endl;

    // Create video track and add to connection
    rtc::Description::Video media("video",
                                  rtc::Description::Direction::SendOnly);

    // 根据编码器类型添加相应的codec
    uint8_t payload_type;
    if (video_codec == "h265") {
      media.addH265Codec(97); // Add H.265 codec with payload type 97
      // 设置 H.265 参数（可选）
      media.addH265Codec(97, "level-id-id=93;profile-id=1");
      payload_type = 97;
    } else {
      media.addH264Codec(96); // Add H.264 codec with payload type 96
      // 设置 H.264 参数（可选）
      // media.addH264Codec(96,
      // "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
      payload_type = 96;
    }

    uint32_t video_ssrc = dis(gen);
    std::string cname = "video_" + std::to_string(video_ssrc);
    std::string msid = "stream_" + id;

    media.addSSRC(video_ssrc, cname, msid, cname);

    video_track = pc->addTrack(media);

    // 设置 RTP 媒体处理器
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        video_ssrc, cname,
        payload_type,
        rtc::H264RtpPacketizer::ClockRate); // H.265使用相同的时钟频率

    // 根据编码器类型创建相应的RTP打包器
    std::shared_ptr<rtc::MediaHandler> packetizer;
    if (video_codec == "h265") {
      // 创建 H.265 RTP 打包器
      packetizer = std::make_shared<rtc::H265RtpPacketizer>(
          rtc::NalUnit::Separator::StartSequence, // 使用长起始序列
          rtpConfig);
      std::cout << "Created H.265 RTP packetizer" << std::endl;
    } else {
      // 创建 H.264 RTP 打包器
      packetizer = std::make_shared<rtc::H264RtpPacketizer>(
          rtc::NalUnit::Separator::StartSequence, // 使用长起始序列
          rtpConfig);
      std::cout << "Created H.264 RTP packetizer" << std::endl;
    }

    // 添加 RTCP SR (Sender Report) 报告器
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);

    // 添加 RTCP NACK 响应器
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);

    // 设置轨道的媒体处理器
    video_track->setMediaHandler(packetizer);

    video_track->onOpen([id, video_track, this]() {
      std::cout << "Video track to " << id << " is now open" << std::endl;
      // Keep track of start time for timestamp calculation
      uint64_t start_time =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();

      // 使用add_track_callback支持多peer连接
      video_capturer_->add_track_callback(
          id,
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
            }
          });

      // 恢复采集（如果已启动且没有其他active peer）
      video_capturer_->resume_capture();
    });

    video_track->onClosed([id, this]() {
      std::cout << "Video Track to " << id << " closed" << std::endl;
      // 检查capturer是否仍然有效
      if (video_capturer_) {
        video_capturer_->remove_track_callback(id);
        // 如果没有其他active peer，暂停采集
        if (!video_capturer_->has_track_callbacks()) {
          video_capturer_->pause_capture();
        }
      }
    });
  }

  if (audio_capturer_ != nullptr && audio_capturer_->is_running()) {
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

    audio_track->onOpen([id, audio_track, this]() {
      std::cout << "Audio track to " << id << " is now open" << std::endl;
      // Keep track of start time for timestamp calculation
      uint64_t start_time =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();

      // 使用add_track_callback支持多peer连接
      audio_capturer_->add_track_callback(
          id,
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
            }
          });

      // 恢复采集（如果已启动且没有其他active peer）
      audio_capturer_->resume_capture();
    });

    audio_track->onClosed([id, this]() {
      std::cout << "Audio Track to " << id << " closed" << std::endl;
      // 检查capturer是否仍然有效
      if (audio_capturer_) {
        audio_capturer_->remove_track_callback(id);
        // 如果没有其他active peer，暂停采集
        if (!audio_capturer_->has_track_callbacks()) {
          audio_capturer_->pause_capture();
        }
      }
    });
  }

  // 修复音频接收逻辑
  std::shared_ptr<rtc::Track> audioReceiver = nullptr;
  pc->onTrack([this, audioReceiver](
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
          [this](const rtc::binary &data, const rtc::FrameInfo &info) {
            if (audio_player_ != nullptr) {
              audio_player_->receiveAudioData(data, info);
            }
          });
    }
  });
  // DataChannel 处理
  pc->onDataChannel([id, this](std::shared_ptr<rtc::DataChannel> dc) {
    std::cout << "DataChannel from " << id << " received with label \""
              << dc->label() << "\"" << std::endl;

    dc->onOpen([id, dc]() {
      std::cout << "DataChannel from " << id << " open" << std::endl;
    });

    dc->onClosed([id, this]() {
      std::cout << "DataChannel from " << id << " closed" << std::endl;
      dataChannelMap_.erase(id);
    });

    dc->onMessage([id, this](auto data) {
      if (std::holds_alternative<std::string>(data)) {
        const std::string &str_data = std::get<std::string>(data);
        try {
          json msg = json::parse(str_data);

          if (msg.contains("type")) {
            std::string type = msg["type"];

            if (type == "video_config") {
              std::cout << "Received video config from " << id << std::endl;

              if (video_capturer_) {
                std::string resolution = msg.value("resolution", "640x480");
                int fps = msg.value("fps", 30);
                int bitrate = msg.value("bitrate", 3200000);
                std::string format = msg.value("format", "yuyv422");

                std::cout << "Video config: " << resolution << ", " << fps
                          << "fps, " << bitrate << "bps, " << format << std::endl;

                // 调用视频配置重置方法
                video_capturer_->reconfigure(resolution, fps, bitrate, format);
              }
            }
          }
        } catch (const std::exception &e) {
          std::cerr << "Failed to parse JSON from " << id << ": " << e.what() << std::endl;
        }
      } else if (std::holds_alternative<rtc::binary>(data)) {
        // TODO: 暂不处理二进制数据
      }
    });
    dataChannelMap_.emplace(id, dc);
  });

  // 通道关闭时，停止音视频捕获（仅在最后一个peer关闭时）
  pc->onStateChange(
      [id, wws, this](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Disconnected ||
            state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Closed) {
          std::cout << "PeerConnection " << id << " closed, removing callbacks..." << std::endl;
          // 检查capturer是否仍然有效，移除对应的回调
          if (video_capturer_ && video_capturer_->is_running()) {
            video_capturer_->remove_track_callback(id);
            if (!video_capturer_->has_track_callbacks()) {
              video_capturer_->pause_capture();
            }
          }
          if (audio_capturer_ && audio_capturer_->is_running()) {
            audio_capturer_->remove_track_callback(id);
            if (!audio_capturer_->has_track_callbacks()) {
              audio_capturer_->pause_capture();
            }
          }
        }
        if (state == rtc::PeerConnection::State::Connected) {
          std::cout << "PeerConnection connected" << std::endl;
          // 连接成功时，停止重连线程
          std::lock_guard<std::mutex> lock(reconnectMutex_);
          if (reconnectThreads_.find(id) != reconnectThreads_.end()) {
            auto& thread = reconnectThreads_[id];
            if (thread && thread->joinable()) {
              std::cout << "Stopping reconnect thread for " << id << std::endl;
              // 设置线程退出标志（通过关闭 ws 来触发）
            }
            reconnectThreads_.erase(id);
          }
        }
      });
  // 记录 PeerConnection
  peerConnectionMap_.emplace(id, pc);
  return pc;
}

WebRTCPublisher::WebRTCPublisher(const std::string &client_id, Cmdline params)
    : client_id_(client_id), params_(params) {
  rtc::InitLogger(rtc::LogLevel::Info);
  localId_ = client_id;
  size_t queue_size = params.framerate()*2;
  // Initialize video capturer
  if (!params.inputDevice().empty()) {
    video_capturer_ = new VideoCapturer(params.inputDevice(), params.debug(),
                                        params.resolution(), params.framerate(),
                                        params.videoFormat(), queue_size,
                                        queue_size, queue_size);
    // 设置视频编码器类型
    video_capturer_->set_video_codec(params.videoCodec());
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

// 析构函数
WebRTCPublisher::~WebRTCPublisher() {
  stop();
}

void WebRTCPublisher::start() {
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

  // 创建 WebSocket
  ws_ = std::make_shared<rtc::WebSocket>();

  std::promise<void> wsPromise;
  auto wsFuture = wsPromise.get_future();

  // 设置 WebSocket 回调和消息处理
  setupWebSocketCallbacks(ws_, wsPromise);

  // 连接服务器
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
  // 测试线程，10s后手动关闭ws
  // std::thread testThread([&]() {
  //   std::this_thread::sleep_for(std::chrono::seconds(10));
  //   std::cout << "Closing WebSocket connection after 10 seconds" << std::endl;
  //   ws_->close();
  // });
  //
  // testThread.join();
}

// 创建 ICE 配置
rtc::Configuration WebRTCPublisher::createIceConfig() {
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

  return config;
}

// 设置 WebSocket 回调和消息处理
void WebRTCPublisher::setupWebSocketCallbacks(std::shared_ptr<rtc::WebSocket> ws,
                                          std::promise<void>& wsPromise) {
  // 获取 ICE 配置
  auto config = createIceConfig();
  auto wws = make_weak_ptr(ws);

  ws->onOpen([this, &wsPromise, ws]() {
    std::cout << "WebSocket connected, signaling ready" << std::endl;
    // 替换旧的 WebSocket
    if (ws_ && ws_ != ws) {
      std::cout << "Replacing old WebSocket" << std::endl;
      ws_->close();
    }
    ws_ = ws;
    // WebSocket 连接成功，停止重连
    this->stopWsReconnect();
    wsPromise.set_value();
  });

  ws->onError([&wsPromise](std::string s) {
    std::cout << "WebSocket error: " << s << std::endl;
    wsPromise.set_exception(std::make_exception_ptr(std::runtime_error(s)));
  });

  ws->onClosed([this]() {
    std::cout << "WebSocket closed" << std::endl;
    // 启动 WebSocket 自动重连
    this->startWsReconnect();
  });

  ws->onMessage([config, wws, this](auto data) {
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
    if (auto jt = peerConnectionMap_.find(id); jt != peerConnectionMap_.end()) {
      if (type == "offer") {
        std::cout << "Release old pc" << std::endl;
        peerConnectionMap_[id]->close();
        peerConnectionMap_.erase(id);
        std::cout << "Answering to " + id << std::endl;
        pc = createPeerConnection(config, wws, id);
      } else {
        pc = jt->second;
      }
    } else if (type == "offer") {
      std::cout << "Answering to " + id << std::endl;
      pc = createPeerConnection(config, wws, id);
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
}

void WebRTCPublisher::stop() {
  // 停止 WebSocket 重连线程
  stopWsReconnect();

  // 关闭所有PeerConnection以确保回调被清理
  for (auto &pair : peerConnectionMap_) {
    if (pair.second) {
      pair.second->close();
    }
  }
  peerConnectionMap_.clear();

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

void WebRTCPublisher::startWsReconnect() {
  // 如果已经在重连，不重复启动
  if (wsReconnectRunning_.exchange(true)) {
    std::cout << "WebSocket reconnect already running" << std::endl;
    return;
  }

  std::cout << "Starting WebSocket auto-reconnect..." << std::endl;

  // 创建重连线程
  wsReconnectThread_ = std::make_shared<std::thread>([this]() {
    while (wsReconnectRunning_) {
      try {
        // 创建新的 WebSocket
        auto newWs = std::make_shared<rtc::WebSocket>();

        std::promise<void> wsPromise;
        auto wsFuture = wsPromise.get_future();

        // 设置 WebSocket 回调和消息处理（使用封装的函数）
        setupWebSocketCallbacks(newWs, wsPromise);

        // 连接服务器
        std::string webSocketServer = params_.webSocketServer();
        int webSocketPort = params_.webSocketPort();
        const std::string wsPrefix =
            webSocketServer.find("://") == std::string::npos ? "ws://" : "";
        const std::string url = wsPrefix + webSocketServer + ":" +
                              std::to_string(webSocketPort) + "/" + client_id_;

        std::cout << "Attempting to reconnect to: " << url << std::endl;
        newWs->open(url);

        // 等待连接成功
        if (wsFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
          std::cout << "WebSocket reconnection successful!" << std::endl;
          break; // 连接成功，退出重连循环
        } else {
          std::cout << "WebSocket reconnection timeout, retrying in 3 seconds..." << std::endl;
        }

      } catch (const std::exception &e) {
        std::cerr << "WebSocket reconnect error: " << e.what() << std::endl;
      }

      // 等待 3 秒后重试
      for (int i = 0; i < 30 && wsReconnectRunning_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  });

  wsReconnectThread_->detach();
}

void WebRTCPublisher::stopWsReconnect() {
  wsReconnectRunning_ = false;
  if (wsReconnectThread_ && wsReconnectThread_->joinable()) {
    std::cout << "Stopping WebSocket reconnect thread..." << std::endl;
    wsReconnectThread_->join();
    wsReconnectThread_.reset();
    std::cout << "WebSocket reconnect thread stopped" << std::endl;
  }
}