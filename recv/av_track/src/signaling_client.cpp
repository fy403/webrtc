#include "signaling_client.h"
#include <iostream>
#include <chrono>
#include <thread>

SignalingClient::SignalingClient() {}

SignalingClient::~SignalingClient() {
    disconnect();
}

bool SignalingClient::connect(const std::string& url, const std::string& local_id) {
    try {
        local_id_ = local_id;
        
        // Construct full WebSocket URL with path
        std::string ws_url = url;
        if (ws_url.back() != '/') {
            ws_url += '/';
        }
        ws_url += local_id;
        
        std::cout << "[Signaling] Connecting to: " << ws_url << std::endl;
        
        // Create WebSocket connection
        ws_ = std::make_shared<rtc::WebSocket>();
        
        setup_callbacks();
        
        ws_->open(ws_url);
        
        // Wait briefly for connection (async)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Signaling] Connection failed: " << e.what() << std::endl;
        connected_.store(false);
        return false;
    }
}

void SignalingClient::disconnect() {
    if (ws_) {
        try {
            ws_->close();
        } catch (...) {}
        ws_.reset();
    }
    connected_.store(false);
}

bool SignalingClient::send_message(const json& message) {
    if (!ws_ || !connected_.load()) {
        std::cerr << "[Signaling] Cannot send: not connected" << std::endl;
        return false;
    }
    
    try {
        std::string payload = message.dump();
        ws_->send(payload);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Signaling] Send error: " << e.what() << std::endl;
        return false;
    }
}

void SignalingClient::setup_callbacks() {
    if (!ws_) return;
    
    ws_->onOpen([this]() {
        std::cout << "[Signaling] Connected to signaling server" << std::endl;
        connected_.store(true);
        
        if (on_state_change_) {
            on_state_change_(true);
        }
    });
    
    ws_->onClosed([this]() {
        std::cout << "[Signaling] Disconnected from signaling server" << std::endl;
        connected_.store(false);
        
        if (on_state_change_) {
            on_state_change_(false);
        }
    });
    
    ws_->onError([this](std::string err) {
        std::cerr << "[Signaling] Error: " << err << std::endl;
        connected_.store(false);
    });
    
    ws_->onMessage([this](rtc::message_variant data) {
        try {
            // Handle binary messages (skip)
            if (std::holds_alternative<std::vector<std::byte>>(data)) {
                return;
            }
            
            // Parse JSON text message
            std::string text = std::get<std::string>(data);
            json msg = json::parse(text);
            
            // Debug output
            std::string type = msg.value("type", "unknown");
            std::string id = msg.value("id", "");
            std::cout << "[Signaling] Received: type=" << type 
                      << ", id=" << id << std::endl;
            
            // Forward to callback
            if (on_message_) {
                on_message_(msg);
            }
        } catch (const json::parse_error& e) {
            std::cerr << "[Signaling] JSON parse error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[Signaling] Message handler error: " << e.what() << std::endl;
        }
    });
}
