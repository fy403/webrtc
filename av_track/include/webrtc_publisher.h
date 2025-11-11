#ifndef WEBRTC_PUBLISHER_H
#define WEBRTC_PUBLISHER_H

#include <memory>
#include <random> // 添加随机数头文件
#include <string>
#include <unordered_map>

#include "audio_capturer.h"
#include "audio_player.h"
#include "nlohmann/json.hpp"
#include "parse_cl.h"
#include "rtc/rtc.hpp"
#include "video_capturer.h"

using std::shared_ptr;
using std::weak_ptr;
template <class T> weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr);

using nlohmann::json;

extern std::string localId;
extern std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>>
    peerConnectionMap;

// Helper function to generate a random ID
std::string randomId(size_t length);

// Create and setup a PeerConnection
shared_ptr<rtc::PeerConnection>
createPeerConnection(const rtc::Configuration &config,
                     weak_ptr<rtc::WebSocket> wws, std::string id,
                     VideoCapturer &video_capturer,
                     AudioCapturer *audio_capturer);

class WebRTCPublisher {
public:
  WebRTCPublisher(const std::string &client_id, Cmdline params);
  void start();
  void stop();

  VideoCapturer *video_capturer_ = nullptr;
  AudioCapturer *audio_capturer_ = nullptr;
  AudioPlayer *audio_player_ = nullptr;

private:
  std::string client_id_;
  Cmdline params_;
  std::shared_ptr<rtc::WebSocket> ws_;
};

#endif // WEBRTC_PUBLISHER_H