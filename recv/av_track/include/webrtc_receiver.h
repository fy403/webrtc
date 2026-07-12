#ifndef WEBRTC_RECEIVER_H
#define WEBRTC_RECEIVER_H

#include <memory>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

#include "rtc/rtc.hpp"
#include "config_parser.h"
#include "nlohmann/json.hpp"

class SignalingClient;
class VideoPipeline;
class AudioPipeline;

using json = nlohmann::json;
using TrackCallback = std::function<void(rtc::Track& track, rtc::Description::Direction dir)>;

class WebRTCReceiver {
public:
    WebRTCReceiver(Config* config);
    ~WebRTCReceiver();
    
    // Initialize receiver and connect to signaling server
    bool init(std::shared_ptr<SignalingClient> signaling);
    
    // Shutdown receiver and cleanup
    void shutdown();
    
    // Start receiving from a specific remote peer ID
    bool start_receive(const std::string& remote_id);
    
    // Set callbacks for when tracks are received
    void set_on_video_track(TrackCallback cb) { on_video_track_cb_ = cb; }
    void set_on_audio_track(TrackCallback cb) { on_audio_track_cb_ = cb; }

private:
    Config* config_;
    std::shared_ptr<SignalingClient> signaling_;
    
    std::string local_id_;
    std::string target_remote_id_;  // Remember who we're connecting to
    std::unordered_map<std::string, std::shared_ptr<rtc::PeerConnection>> peer_connections_;
    std::mutex pc_mutex_;
    
    // PC-level reconnection
    std::atomic<bool> reconnecting_{false};
    std::unique_ptr<std::thread> reconnect_thread_;
    
    // Signaling-level reconnection
    std::atomic<bool> signaling_reconnecting_{false};
    std::unique_ptr<std::thread> signaling_reconnect_thread_;
    
    // Create ICE configuration from config_
    rtc::Configuration create_ice_config() const;
    
    // Create a PeerConnection for receiving
    std::shared_ptr<rtc::PeerConnection> create_peer_connection(
        const std::string& remote_id,
        const rtc::Configuration& ice_config);
    
    // Setup PeerConnection callbacks
    void setup_pc_callbacks(
        std::shared_ptr<rtc::PeerConnection> pc,
        const std::string& remote_id);
    
    // Handle incoming offer (create answer)
    void handle_offer(const std::string& id, const std::string& sdp);
    
    // Handle incoming answer
    void handle_answer(const std::string& id, const std::string& sdp);
    
    // Handle ICE candidate
    void handle_candidate(
        const std::string& id,
        const std::string& candidate,
        const std::string& mid);
    
    // Process signaling messages
    void process_signaling_message(const json& msg);
    
    // PC-level auto-reconnect logic
    void start_auto_reconnect(const std::string& remote_id);
    void stop_auto_reconnect();
    
    // Signaling-level auto-reconnect (when signaling server goes down)
    void start_signaling_reconnect();
    void stop_signaling_reconnect();
    
    // Generate random ID helper
    static std::string generate_random_id(size_t length = 8);

    // Track callbacks (set by main.cpp)
    TrackCallback on_video_track_cb_;
    TrackCallback on_audio_track_cb_;
    
    // Receive-only tracks (kept alive so transceivers persist)
    std::shared_ptr<rtc::Track> local_video_track_;
    std::shared_ptr<rtc::Track> local_audio_track_;
};

#endif // WEBRTC_RECEIVER_H
