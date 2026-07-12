#include "webrtc_receiver.h"
#include "signaling_client.h"
#include "video_pipeline.h"
#include "audio_pipeline.h"
#include "latency_tracker.h"

#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>

WebRTCReceiver::WebRTCReceiver(Config* config)
    : config_(config) {}

WebRTCReceiver::~WebRTCReceiver() {
    shutdown();
}

bool WebRTCReceiver::init(std::shared_ptr<SignalingClient> signaling) {
    signaling_ = signaling;
    local_id_ = signaling->get_local_id();
    target_remote_id_ = config_->remote_id;
    
    // Setup signaling message handler
    signaling_->set_on_message(
        [this](const json& msg) { process_signaling_message(msg); }
    );
    
    // Setup signaling disconnection callback → auto-reconnect
    signaling_->set_on_state_change([this](bool connected) {
        if (!connected && config_->auto_reconnect && !target_remote_id_.empty()) {
            std::cerr << "[WebRTC] Signaling lost, starting signaling reconnect..." << std::endl;
            // Stop PC-level reconnect first
            stop_auto_reconnect();
            // Close all peer connections
            {
                std::lock_guard<std::mutex> lock(pc_mutex_);
                for (auto& [id, pc] : peer_connections_) {
                    if (pc) { try { pc->close(); } catch (...) {} }
                }
                peer_connections_.clear();
            }
            start_signaling_reconnect();
        }
    });
    
    std::cout << "[WebRTC] Initialized with local ID: " << local_id_ << std::endl;
    return true;
}

void WebRTCReceiver::shutdown() {
    stop_auto_reconnect();
    stop_signaling_reconnect();
    
    // Release receive-only tracks
    local_video_track_.reset();
    local_audio_track_.reset();
    
    {
        std::lock_guard<std::mutex> lock(pc_mutex_);
        for (auto& [id, pc] : peer_connections_) {
            if (pc) {
                try { pc->close(); } catch (...) {}
            }
        }
        peer_connections_.clear();
    }
    
    std::cout << "[WebRTC] Shutdown complete" << std::endl;
}

bool WebRTCReceiver::start_receive(const std::string& remote_id) {
    if (remote_id.empty()) {
        std::cerr << "[WebRTC] Cannot start receive: no remote ID specified" << std::endl;
        return false;
    }
    
    target_remote_id_ = remote_id;
    std::cout << "[WebRTC] Starting receive from: " << remote_id << std::endl;
    
    auto ice_config = create_ice_config();
    auto pc = create_peer_connection(remote_id, ice_config);
    
    if (!pc) {
        std::cerr << "[WebRTC] Failed to create PeerConnection for " << remote_id << std::endl;
        return false;
    }
    
    // Store peer connection
    {
        std::lock_guard<std::mutex> lock(pc_mutex_);
        peer_connections_[remote_id] = pc;
    }
    
    // Add receive-only tracks so SDP offer has media lines to negotiate
    try {
        // Video: accept H.264 (payload 96) and H.265 (payload 97)
        rtc::Description::Video video_media("video", rtc::Description::Direction::RecvOnly);
        video_media.addH264Codec(96);
        video_media.addH265Codec(97);
        local_video_track_ = pc->addTrack(video_media);
        
        // Audio: accept Opus (payload 111)
        rtc::Description::Audio audio_media("audio", rtc::Description::Direction::RecvOnly);
        audio_media.addOpusCodec(111);
        local_audio_track_ = pc->addTrack(audio_media);
        
        std::cout << "[WebRTC] Added receive tracks: video(H.264/96, H.265/97), audio(Opus/111)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC] Failed to add receive tracks: " << e.what() << std::endl;
        return false;
    }
    
    // ── Install RTP depacketizers + RTCP sessions BEFORE setLocalDescription ──
    // Required! setMediaHandler MUST be called before setLocalDescription 
    // for locally-added RecvOnly tracks to work correctly.
    // 
    // Chain: Depacketizer → RtcpReceivingSession
    //   - Depacketizer: RTP→Annex B frames, delivered via onFrame callback
    //   - RtcpReceivingSession: sends RTCP PLI/NACK feedback to sender
    //
    // Default to H.264; will switch to H.265 in handle_answer if answer says so.
    {
        auto video_handler = std::make_shared<rtc::H264RtpDepacketizer>(
            rtc::NalUnit::Separator::StartSequence);
        video_handler->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
        local_video_track_->setMediaHandler(video_handler);
        std::cout << "[WebRTC] Installed H.264 depacketizer + RTCP session on video track" << std::endl;
    }
    {
        auto audio_handler = std::make_shared<rtc::OpusRtpDepacketizer>(48000);
        audio_handler->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
        local_audio_track_->setMediaHandler(audio_handler);
        std::cout << "[WebRTC] Installed Opus depacketizer + RTCP session on audio track" << std::endl;
    }
    
    // Generate and send an OFFER (publisher is the answerer)
    try {
        std::cout << "[WebRTC] Generating offer for " << remote_id << "..." << std::endl;
        pc->setLocalDescription(rtc::Description::Type::Offer);
        // onLocalDescription callback will send the offer to the publisher via signaling
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC] Failed to generate offer: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

rtc::Configuration WebRTCReceiver::create_ice_config() const {
    rtc::Configuration config;
    
    // Add STUN server (libdatachannel 0.24.x: using constructor)
    config.iceServers.emplace_back(config_->ice.stun_server, 19302);
    
    // Add TURN server if configured
    if (!config_->ice.turn_server.empty()) {
        config.iceServers.emplace_back(
            "tx.fy403.cn", 3478,
            config_->ice.turn_username,
            config_->ice.turn_password,
            rtc::IceServer::RelayType::TurnUdp
        );
    }
    
    return config;
}

std::shared_ptr<rtc::PeerConnection> WebRTCReceiver::create_peer_connection(
    const std::string& remote_id,
    const rtc::Configuration& ice_config)
{
    try {
        auto pc = std::make_shared<rtc::PeerConnection>(ice_config);
        
        setup_pc_callbacks(pc, remote_id);
        
        std::cout << "[WebRTC] PeerConnection created for " << remote_id << std::endl;
        return pc;
        
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC] Failed to create PeerConnection: " << e.what() << std::endl;
        return nullptr;
    }
}

void WebRTCReceiver::setup_pc_callbacks(
    std::shared_ptr<rtc::PeerConnection> pc,
    const std::string& remote_id)
{
    // Local description callback (for sending offer/answer)
    pc->onLocalDescription([this, remote_id](rtc::Description desc) {
        std::cout << "[WebRTC] Sending " << desc.typeString() << " to " << remote_id << std::endl;
        
        json msg = {
            {"id", remote_id},
            {"type", desc.typeString()},
            {"description", std::string(desc)}
        };
        
        if (signaling_ && signaling_->is_connected()) {
            signaling_->send_message(msg);
        }
    });
    
    // Local ICE candidate callback
    pc->onLocalCandidate([this, remote_id](rtc::Candidate cand) {
        std::cout << "[WebRTC] Local candidate for " << remote_id << ": " 
                  << cand.candidate() << std::endl;
        
        json msg = {
            {"id", remote_id},
            {"type", "candidate"},
            {"candidate", std::string(cand)},
            {"mid", cand.mid()}
        };
        
        if (signaling_ && signaling_->is_connected()) {
            signaling_->send_message(msg);
        }
    });
    
    // ICE state change (0.24.x: IceState is nested under PeerConnection)
    pc->onIceStateChange([remote_id](rtc::PeerConnection::IceState state) {
        std::cout << "[WebRTC] ICE state (" << remote_id << "): " << static_cast<int>(state) << std::endl;
    });
    
    // Gathering state change
    pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
        std::cout << "[WebRTC] Gathering state: " << static_cast<int>(state) << std::endl;
    });
    
    // **KEY CALLBACK**: Remote track received (libdatachannel 0.24.x: single param)
    pc->onTrack([this, remote_id](std::shared_ptr<rtc::Track> track) {
        // libdatachannel 0.24.x: no kind() — detect media type from SDP m= line
        auto desc_str = track->description().description();
        bool is_video = (desc_str.find("m=video") != std::string::npos);
        bool is_audio = (desc_str.find("m=audio") != std::string::npos);
        const char* kind_str = is_video ? "video" : (is_audio ? "audio" : "unknown");

        std::cout << "[WebRTC] Received track: kind=" << kind_str
                  << ", id=" << track->mid()
                  << ", direction=" << track->direction() << std::endl;
        
        if (track->direction() == rtc::Description::Direction::RecvOnly ||
            track->direction() == rtc::Description::Direction::SendRecv)
        {
            // Notify callback about the new track (caller sets up onMessage)
            if (is_video && on_video_track_cb_) {
                on_video_track_cb_(*track, track->direction());
            } else if (is_audio && on_audio_track_cb_) {
                on_audio_track_cb_(*track, track->direction());
            }
        }
    });
    
    // Data channel (optional, for control messages)
    pc->onDataChannel([remote_id](std::shared_ptr<rtc::DataChannel> dc) {
        std::cout << "[WebRTC] DataChannel received from " 
                  << remote_id << ": " << dc->label() << std::endl;
        
        dc->onMessage([dc, remote_id](rtc::message_variant data) {
            if (std::holds_alternative<std::string>(data)) {
                std::string text = std::get<std::string>(data);
                std::cout << "[DataChannel] From " << remote_id 
                          << ": " << text.substr(0, 50) << std::endl;
            }
        });
    });
    
    // State change
    pc->onStateChange([this, remote_id](rtc::PeerConnection::State state) {
        std::cout << ("[WebRTC] Connection state (" + remote_id + "): ")
                  << static_cast<int>(state) << std::endl;
        
        if (state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Disconnected)
        {
            const char* reason = (state == rtc::PeerConnection::State::Failed) 
                                 ? "failed" : "disconnected";
            std::cerr << "[WebRTC] Connection " << reason << " with " << remote_id << std::endl;
            
            if (config_->auto_reconnect) {
                // Check if signaling is still alive
                if (signaling_ && signaling_->is_connected()) {
                    std::cout << "[WebRTC] Signaling OK, starting PC reconnect..." << std::endl;
                    start_auto_reconnect(remote_id);
                } else {
                    std::cerr << "[WebRTC] Signaling also down, waiting for signaling reconnect..." << std::endl;
                    // Signaling reconnect callback will handle this
                }
            }
        }
        
        if (state == rtc::PeerConnection::State::Closed) {
            // Don't trigger reconnect for intentional close
            std::lock_guard<std::mutex> lock(pc_mutex_);
            peer_connections_.erase(remote_id);
        }
    });
}

void WebRTCReceiver::handle_offer(const std::string& id, const std::string& sdp) {
    std::cout << "[WebRTC] Handling offer from " << id << std::endl;
    
    std::shared_ptr<rtc::PeerConnection> pc;
    
    {
        std::lock_guard<std::mutex> lock(pc_mutex_);
        auto it = peer_connections_.find(id);
        if (it == peer_connections_.end()) {
            // Create new PC if doesn't exist yet
            auto ice_config = create_ice_config();
            pc = create_peer_connection(id, ice_config);
            if (pc) {
                peer_connections_[id] = pc;
            }
        } else {
            pc = it->second;
        }
    }
    
    if (!pc) {
        std::cerr << "[WebRTC] No PeerConnection available for " << id << std::endl;
        return;
    }
    
    // Set remote description (offer)
    try {
        pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
        std::cout << "[WebRTC] Remote offer set, creating answer..." << std::endl;
        
        // Create answer (triggers onLocalDescription callback)
        pc->setLocalDescription(rtc::Description::Type::Answer);
        
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC] Error handling offer: " << e.what() << std::endl;
    }
}

void WebRTCReceiver::handle_answer(const std::string& id, const std::string& sdp) {
    std::cout << "[WebRTC] Handling answer from " << id << std::endl;
    
    std::shared_ptr<rtc::PeerConnection> pc;
    {
        std::lock_guard<std::mutex> lock(pc_mutex_);
        auto it = peer_connections_.find(id);
        if (it == peer_connections_.end()) {
            std::cerr << "[WebRTC] Unknown peer: " << id << std::endl;
            return;
        }
        pc = it->second;
    }
    
    try {
        // Debug: print answer SDP content
        std::cout << "[WebRTC] === Answer SDP BEGIN ===" << std::endl;
        std::cout << sdp << std::endl;
        std::cout << "[WebRTC] === Answer SDP END ===" << std::endl;
        
        pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
        std::cout << "[WebRTC] Remote answer applied, activating receive tracks..." << std::endl;
        
        // ── Detect negotiated video codec from answer SDP ──
        bool use_h265 = false;
        {
            std::string sdp_lower = sdp;
            std::transform(sdp_lower.begin(), sdp_lower.end(), sdp_lower.begin(), ::tolower);
            auto video_pos = sdp_lower.find("m=video");
            if (video_pos != std::string::npos) {
                auto next_m = sdp_lower.find("\nm=", video_pos + 1);
                std::string video_section = (next_m != std::string::npos)
                    ? sdp_lower.substr(video_pos, next_m - video_pos)
                    : sdp_lower.substr(video_pos);
                if (video_section.find("h265") != std::string::npos ||
                    video_section.find("hevc") != std::string::npos) {
                    use_h265 = true;
                }
            }
        }
        
        // ── Update video depacketizer if H.265 was negotiated ──
        // (H.264 + audio were installed in start_receive() before setLocalDescription)
        if (local_video_track_ && use_h265) {
            auto video_handler = std::make_shared<rtc::H265RtpDepacketizer>(
                rtc::NalUnit::Separator::StartSequence);
            video_handler->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
            local_video_track_->setMediaHandler(video_handler);
            std::cout << "[WebRTC] Switched video depacketizer: H.264 → H.265 (Annex B)" << std::endl;
        } else if (local_video_track_) {
            std::cout << "[WebRTC] Video depacketizer: H.264 (Annex B) [already installed]" << std::endl;
        }
        
        // Setup track-open callback to confirm media negotiation
        if (local_video_track_) {
            local_video_track_->onOpen([this]() {
                std::cout << "[WebRTC] 🟢 Video track OPEN — media negotiation complete" << std::endl;
            });
            local_video_track_->onClosed([this]() {
                std::cout << "[WebRTC] 🔴 Video track CLOSED" << std::endl;
            });
        }
        if (local_audio_track_) {
            local_audio_track_->onOpen([this]() {
                std::cout << "[WebRTC] 🟢 Audio track OPEN — media negotiation complete" << std::endl;
            });
            local_audio_track_->onClosed([this]() {
                std::cout << "[WebRTC] 🔴 Audio track CLOSED" << std::endl;
            });
        }
        
        // Locally-added RecvOnly tracks DON'T trigger onTrack() because they
        // already exist — wire up onMessage callbacks directly here.
        if (local_video_track_ && on_video_track_cb_) {
            std::cout << "[WebRTC] Activating video track data reception" << std::endl;
            on_video_track_cb_(*local_video_track_, rtc::Description::Direction::RecvOnly);
        }
        if (local_audio_track_ && on_audio_track_cb_) {
            std::cout << "[WebRTC] Activating audio track data reception" << std::endl;
            on_audio_track_cb_(*local_audio_track_, rtc::Description::Direction::RecvOnly);
        }
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC] Error setting answer: " << e.what() << std::endl;
    }
}

void WebRTCReceiver::handle_candidate(
    const std::string& id,
    const std::string& candidate,
    const std::string& mid)
{
    std::shared_ptr<rtc::PeerConnection> pc;
    {
        std::lock_guard<std::mutex> lock(pc_mutex_);
        auto it = peer_connections_.find(id);
        if (it == peer_connections_.end()) return;
        pc = it->second;
    }
    
    try {
        pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC] Error adding candidate: " << e.what() << std::endl;
    }
}

void WebRTCReceiver::process_signaling_message(const json& msg) {
    try {
        std::string id = msg.value("id", "");
        std::string type = msg.value("type", "");
        
        if (type == "offer") {
            handle_offer(id, msg.value("description", ""));
        } else if (type == "answer") {
            handle_answer(id, msg.value("description", ""));
        } else if (type == "candidate") {
            handle_candidate(id, 
                           msg.value("candidate", ""),
                           msg.value("mid", ""));
        } else {
            std::cout << "[WebRTC] Unknown message type: " << type << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC] Error processing message: " << e.what() << std::endl;
    }
}

void WebRTCReceiver::start_auto_reconnect(const std::string& remote_id) {
    stop_auto_reconnect();  // Stop existing reconnection first
    
    if (reconnecting_.load()) return;  // Already reconnecting
    
    reconnecting_.store(true);
    reconnect_thread_ = std::make_unique<std::thread>(
        [this, remote_id]() {
            int attempts = 0;
            int delay_ms = config_->reconnect_interval_min;
            
            while (attempts++ < config_->max_reconnect_attempts && reconnecting_.load()) {
                // Re-check signalling is connected
                if (!signaling_ || !signaling_->is_connected()) {
                    std::cerr << "[Reconnect] Signaling not connected, aborting PC reconnect" << std::endl;
                    break;
                }
                
                std::cout << "[Reconnect] PC attempt " << attempts 
                          << "/" << config_->max_reconnect_attempts
                          << " for " << remote_id 
                          << " (delay=" << delay_ms << "ms)" << std::endl;
                
                // Check if we already have a working connection
                {
                    std::lock_guard<std::mutex> lock(pc_mutex_);
                    auto it = peer_connections_.find(remote_id);
                    if (it != peer_connections_.end() &&
                        it->second->state() == rtc::PeerConnection::State::Connected)
                    {
                        std::cout << "[Reconnect] Already connected to " 
                                  << remote_id << std::endl;
                        break;
                    }
                }
                
                // Remove old PC and create new one
                std::shared_ptr<rtc::PeerConnection> new_pc;
                {
                    std::lock_guard<std::mutex> lock(pc_mutex_);
                    auto it = peer_connections_.find(remote_id);
                    if (it != peer_connections_.end()) {
                        it->second->close();
                        peer_connections_.erase(it);
                    }
                    
                    auto ice_config = create_ice_config();
                    new_pc = create_peer_connection(remote_id, ice_config);
                    if (new_pc) {
                        peer_connections_[remote_id] = new_pc;
                    }
                }
                
                // Add receive tracks and send offer
                if (new_pc) {
                    try {
                        // Add receive-only tracks
                        rtc::Description::Video video_media("video", rtc::Description::Direction::RecvOnly);
                        video_media.addH264Codec(96);
                        video_media.addH265Codec(97);
                        local_video_track_ = new_pc->addTrack(video_media);
                        
                        rtc::Description::Audio audio_media("audio", rtc::Description::Direction::RecvOnly);
                        audio_media.addOpusCodec(111);
                        local_audio_track_ = new_pc->addTrack(audio_media);
                        
                        // Install RTP depacketizers + RTCP sessions BEFORE setLocalDescription
                        {
                            auto video_handler = std::make_shared<rtc::H264RtpDepacketizer>(
                                rtc::NalUnit::Separator::StartSequence);
                            video_handler->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
                            local_video_track_->setMediaHandler(video_handler);
                        }
                        {
                            auto audio_handler = std::make_shared<rtc::OpusRtpDepacketizer>(48000);
                            audio_handler->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
                            local_audio_track_->setMediaHandler(audio_handler);
                        }
                        
                        std::cout << "[Reconnect] Sending offer to " << remote_id << std::endl;
                        new_pc->setLocalDescription(rtc::Description::Type::Offer);
                    } catch (const std::exception& e) {
                        std::cerr << "[Reconnect] Failed: " << e.what() << std::endl;
                    }
                }
                
                // Wait before next attempt
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                
                // Increase delay (linear backoff)
                delay_ms = std::min(delay_ms + 100, config_->reconnect_interval_max);
            }
            
            reconnecting_.store(false);
            std::cout << "[Reconnect] PC reconnect finished for " << remote_id << std::endl;
        }
    );
}

void WebRTCReceiver::stop_auto_reconnect() {
    reconnecting_.store(false);
    
    if (reconnect_thread_ && reconnect_thread_->joinable()) {
        reconnect_thread_->detach();
    }
    reconnect_thread_.reset();
}

void WebRTCReceiver::start_signaling_reconnect() {
    stop_signaling_reconnect();
    
    if (signaling_reconnecting_.load()) return;
    signaling_reconnecting_.store(true);
    
    signaling_reconnect_thread_ = std::make_unique<std::thread>([this]() {
        int attempts = 0;
        int delay_ms = config_->reconnect_interval_min;
        const std::string url = config_->signaling_url;
        const std::string remote_id = target_remote_id_;
        
        while (attempts++ < config_->max_reconnect_attempts && signaling_reconnecting_.load()) {
            std::cout << "[Reconnect] Signaling attempt " << attempts 
                      << "/" << config_->max_reconnect_attempts
                      << " (delay=" << delay_ms << "ms)" << std::endl;
            
            // Disconnect old socket and reconnect
            signaling_->disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            signaling_->connect(url, local_id_);
            
            // Poll for connection (max 5 seconds)
            bool connected = false;
            for (int i = 0; i < 50 && signaling_reconnecting_.load(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (signaling_->is_connected()) {
                    connected = true;
                    break;
                }
            }
            
            if (connected) {
                std::cout << "[Reconnect] Signaling reconnected, re-establishing WebRTC..." << std::endl;
                if (!remote_id.empty()) {
                    start_receive(remote_id);
                }
                break;
            }
            
            std::cout << "[Reconnect] Signaling reconnect failed, retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            delay_ms = std::min(delay_ms + 200, config_->reconnect_interval_max);
        }
        
        signaling_reconnecting_.store(false);
        std::cout << "[Reconnect] Signaling reconnect finished" << std::endl;
    });
}

void WebRTCReceiver::stop_signaling_reconnect() {
    signaling_reconnecting_.store(false);
    
    if (signaling_reconnect_thread_ && signaling_reconnect_thread_->joinable()) {
        signaling_reconnect_thread_->detach();
    }
    signaling_reconnect_thread_.reset();
}

std::string WebRTCReceiver::generate_random_id(size_t length) {
    static const char chars[] = 
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
    
    std::string id(length, '0');
    std::generate_n(id.begin(), length, [&]() { return chars[dist(gen)]; });
    
    return id;
}
