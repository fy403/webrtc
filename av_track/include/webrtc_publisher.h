#ifndef WEBRTC_PUBLISHER_H
#define WEBRTC_PUBLISHER_H

#include <string>
#include <memory>
#include <unordered_map>
#include <random> // 添加随机数头文件

#include "video_capturer.h"
#include "parse_cl.h"
#include "rtc/rtc.hpp"
#include "nlohmann/json.hpp"

using std::shared_ptr;
using std::weak_ptr;
template <class T>
weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr);

using nlohmann::json;

extern std::string localId;
extern std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>> peerConnectionMap;

// Helper function to generate a random ID
std::string randomId(size_t length);

// Create and setup a PeerConnection
shared_ptr<rtc::PeerConnection> createPeerConnection(const rtc::Configuration &config,
                                                     weak_ptr<rtc::WebSocket> wws, std::string id,
                                                     VideoCapturer &video_capturer);

class WebRTCPublisher
{
public:
    WebRTCPublisher(const std::string &client_id, Cmdline params, const std::string &input_device);
    void start();
    void stop();

    VideoCapturer video_capturer_;

private:
    std::string client_id_;
    Cmdline params_;
    std::shared_ptr<rtc::WebSocket> ws_;
};

#endif // WEBRTC_PUBLISHER_H