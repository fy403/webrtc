#ifndef WEBRTC_PUBLISHER_H
#define WEBRTC_PUBLISHER_H

#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>

#include "audio_capturer.h"
#include "audio_player.h"
#include "nlohmann/json.hpp"
#include "parse_cl.h"
#include "rtc/rtc.hpp"
#include "video_capturer.h"

using std::shared_ptr;
using std::weak_ptr;

using nlohmann::json;

// Helper function to generate a random ID
std::string randomId(size_t length);

class WebRTCPublisher {
public:
  WebRTCPublisher(const std::string &client_id, Cmdline params);
  ~WebRTCPublisher();
  
  void start();
  void stop();

  VideoCapturer *video_capturer_ = nullptr;
  AudioCapturer *audio_capturer_ = nullptr;
  AudioPlayer *audio_player_ = nullptr;

  // 获取 PeerConnection 映射
  const std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>>& getPeerConnectionMap() const {
    return peerConnectionMap_;
  }

  // 获取 DataChannel 映射
  const std::unordered_map<std::string, std::shared_ptr<rtc::DataChannel>>& getDataChannelMap() const {
    return dataChannelMap_;
  }

private:
  std::string client_id_;
  Cmdline params_;
  std::shared_ptr<rtc::WebSocket> ws_;

  // 全局状态管理
  std::string localId_;
  std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>> peerConnectionMap_;
  std::unordered_map<std::string, std::shared_ptr<rtc::DataChannel>> dataChannelMap_;
  
  // 重连状态管理
  std::unordered_map<std::string, std::shared_ptr<std::thread>> reconnectThreads_;
  std::mutex reconnectMutex_;

  // WebSocket 重连相关
  std::shared_ptr<std::thread> wsReconnectThread_;
  std::atomic<bool> wsReconnectRunning_{false};

  // WebSocket 自动重连
  void startWsReconnect();
  void stopWsReconnect();

  // WebSocket 设置和消息处理（封装以供重连复用）
  void setupWebSocketCallbacks(std::shared_ptr<rtc::WebSocket> ws, std::promise<void>& wsPromise);
  rtc::Configuration createIceConfig();

  // 创建并设置 PeerConnection
  shared_ptr<rtc::PeerConnection> createPeerConnection(
      const rtc::Configuration &config,
      weak_ptr<rtc::WebSocket> wws,
      const std::string &id);
};

#endif // WEBRTC_PUBLISHER_H