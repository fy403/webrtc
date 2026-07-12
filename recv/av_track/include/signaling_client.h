#ifndef SIGNALING_CLIENT_H
#define SIGNALING_CLIENT_H

#include <memory>
#include <functional>
#include <atomic>

#include "rtc/rtc.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

using MessageCallback = std::function<void(const json&)>;
using StateChangeCallback = std::function<void(bool connected)>;

class SignalingClient {
public:
    SignalingClient();
    ~SignalingClient();

    // Connect to signaling server with given URL and local ID
    bool connect(const std::string& url, const std::string& local_id);
    
    // Disconnect from server
    void disconnect();
    
    // Check if connected
    bool is_connected() const { return connected_.load(); }
    
    // Get local ID assigned by server
    const std::string& get_local_id() const { return local_id_; }
    
    // Send JSON message through WebSocket
    bool send_message(const json& message);
    
    // Set callback for received messages (called from WS thread)
    void set_on_message(MessageCallback cb) { on_message_ = cb; }
    
    // Set callback for connection state changes
    void set_on_state_change(StateChangeCallback cb) { on_state_change_ = cb; }

private:
    std::shared_ptr<rtc::WebSocket> ws_;
    std::string local_id_;
    std::atomic<bool> connected_{false};
    
    MessageCallback on_message_;
    StateChangeCallback on_state_change_;
    
    // Setup WebSocket callbacks internally
    void setup_callbacks();
};

#endif // SIGNALING_CLIENT_H
