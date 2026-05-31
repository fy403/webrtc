#include "rtc/rtc.hpp"
#include "config_parser.h"
#include "shell_executor.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std::chrono_literals;
using std::shared_ptr;
using std::weak_ptr;

using nlohmann::json;

// =============================================================================
// Global state
// =============================================================================
std::atomic<bool> g_shutdown_requested{false};

// WebSocket reconnect
std::shared_ptr<std::thread> g_ws_reconnect_thread;
std::atomic<bool> g_ws_reconnect_running{false};
std::shared_ptr<rtc::WebSocket> g_current_ws;
std::string g_client_id;
std::shared_ptr<Config> g_config = nullptr;

// WebSocket heartbeat and timeout
std::shared_ptr<std::thread> g_ws_heartbeat_thread;
std::atomic<bool> g_ws_heartbeat_running{false};
std::atomic<uint64_t> g_last_ws_activity{0};
constexpr uint64_t WS_HEARTBEAT_INTERVAL = 1;
constexpr uint64_t WS_TIMEOUT_SECONDS = 5;

// PeerConnection and DataChannel maps
std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>> peerConnectionMap;
std::unordered_map<std::string, shared_ptr<rtc::DataChannel>> dataChannelMap;

// Shell sessions (one per DataChannel connection)
std::unordered_map<std::string, std::shared_ptr<ShellSession>> shellSessionMap;
std::mutex g_shell_mutex;

// =============================================================================
// Signal handler
// =============================================================================
void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down gracefully..."
              << std::endl;
    g_shutdown_requested.store(true);
}

void setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

// =============================================================================
// Forward declarations
// =============================================================================
template<class T>
weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }

std::string randomId(size_t length);
shared_ptr<rtc::PeerConnection> createPeerConnection(
    const rtc::Configuration &config,
    weak_ptr<rtc::WebSocket> wws,
    std::string id);
rtc::Configuration createIceConfig();
void setupWebSocketCallbacks(std::shared_ptr<rtc::WebSocket> ws,
                             std::shared_ptr<std::promise<void>> wsPromise);
void startWsReconnect();
void stopWsReconnect();
void startWsHeartbeat();
void stopWsHeartbeat();

// =============================================================================
// main
// =============================================================================
int main(int argc, char **argv) {
    try {
        setup_signal_handlers();

        if (argc != 2) {
            std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
            std::cerr << "Example: " << argv[0] << " config.txt" << std::endl;
            return 1;
        }

        std::string configFile = argv[1];
        g_config = std::make_shared<Config>(configFile);
        g_config->load();
        g_config->display();

        std::string client_id = g_config->get("client_id");
        if (client_id.empty()) {
            client_id = randomId(4);
            std::cout << "Generated client ID: " << client_id << std::endl;
        } else {
            std::cout << "Using specified client ID: " << client_id << std::endl;
        }

        // rtc initialization
        rtc::InitLogger(rtc::LogLevel::Info);

        if (g_shutdown_requested.load()) {
            std::cout << "Shutdown requested before WebSocket connection established"
                      << std::endl;
            return 0;
        }

        g_client_id = client_id;

        // Create WebSocket
        auto ws = std::make_shared<rtc::WebSocket>();
        auto wsPromise = std::make_shared<std::promise<void>>();
        auto wsFuture = wsPromise->get_future();

        setupWebSocketCallbacks(ws, wsPromise);

        std::string wsHost = g_config->get("webSocketServer", "localhost");
        int wsPort = g_config->getAsInt("webSocketPort", 8000);
        const std::string wsPrefix = wsHost.find("://") == std::string::npos ? "ws://" : "";
        const std::string url = wsPrefix + wsHost + ":" + std::to_string(wsPort) + "/" + client_id;

        std::cout << "WebSocket URL is " << url << std::endl;
        ws->open(url);

        std::cout << "Waiting for signaling to be connected..." << std::endl;
        wsFuture.get();

        g_last_ws_activity.store(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );

        startWsHeartbeat();

        if (g_shutdown_requested.load()) {
            throw std::runtime_error("Shutdown requested before main loop started");
        }

        std::cout << "CmdTrack is ready. Client ID: " << client_id << std::endl;
        std::cout << "Waiting for shell commands..." << std::endl;

        // Main loop
        while (!g_shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "Cleaning up..." << std::endl;
        stopWsHeartbeat();
        stopWsReconnect();

        // Destroy shell sessions before closing connections
        {
            std::lock_guard<std::mutex> lock(g_shell_mutex);
            shellSessionMap.clear();
        }
        dataChannelMap.clear();
        peerConnectionMap.clear();

        if (ws) ws->close();
        return 0;

    } catch (const std::exception &e) {
        std::cout << "Error: " << e.what() << std::endl;
        {
            std::lock_guard<std::mutex> lock(g_shell_mutex);
            shellSessionMap.clear();
        }
        dataChannelMap.clear();
        peerConnectionMap.clear();
        return -1;
    }
}

// =============================================================================
// PeerConnection
// =============================================================================
shared_ptr<rtc::PeerConnection> createPeerConnection(
    const rtc::Configuration &config,
    weak_ptr<rtc::WebSocket> wws,
    std::string id) {

    auto pc = std::make_shared<rtc::PeerConnection>(config);

    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "State: " << state << std::endl;
    });

    pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
        std::cout << "Gathering State: " << state << std::endl;
    });

    pc->onLocalDescription([wws, id](rtc::Description description) {
        json message = {
            {"id", id},
            {"type", description.typeString()},
            {"description", std::string(description)}
        };

        if (auto ws = wws.lock())
            ws->send(message.dump());
    });

    pc->onLocalCandidate([wws, id](rtc::Candidate candidate) {
        json message = {
            {"id", id},
            {"type", "candidate"},
            {"candidate", std::string(candidate)},
            {"mid", candidate.mid()}
        };

        if (auto ws = wws.lock())
            ws->send(message.dump());
    });

    pc->onDataChannel([id](shared_ptr<rtc::DataChannel> dc) {
        std::cout << "DataChannel from " << id << " received with label \""
                  << dc->label() << "\"" << std::endl;

        dc->onOpen([id, dc]() {
            std::cout << "DataChannel from " << id << " open" << std::endl;

            // Auto-create a PTY shell session when DataChannel opens
            std::lock_guard<std::mutex> lock(g_shell_mutex);
            // Destroy old session if exists (reconnect scenario)
            shellSessionMap.erase(id);
            auto session = std::make_shared<ShellSession>(24, 80, dc);
            if (session->isRunning()) {
                shellSessionMap[id] = session;
            }
        });

        dc->onClosed([id]() {
            std::cout << "DataChannel from " << id << " closed" << std::endl;

            // Destroy shell session
            {
                std::lock_guard<std::mutex> lock(g_shell_mutex);
                shellSessionMap.erase(id);
            }
            dataChannelMap.erase(id);
        });

        dc->onMessage([id, dc](auto data) {
            // ── binary data = raw keystrokes → write to PTY ──────
            if (std::holds_alternative<rtc::binary>(data)) {
                const auto& bin = std::get<rtc::binary>(data);
                std::lock_guard<std::mutex> lock(g_shell_mutex);
                auto it = shellSessionMap.find(id);
                if (it != shellSessionMap.end() && it->second) {
                    it->second->write(bin);
                }
                return;
            }

            // ── string data = JSON control messages ──────────────
            if (std::holds_alternative<std::string>(data)) {
                const std::string &str_data = std::get<std::string>(data);
                try {
                    json msg = json::parse(str_data);

                    if (msg.contains("type")) {
                        std::string type = msg["type"];

                        if (type == "shell_resize") {
                            int rows = msg.value("rows", 24);
                            int cols = msg.value("cols", 80);
                            std::lock_guard<std::mutex> lock(g_shell_mutex);
                            auto it = shellSessionMap.find(id);
                            if (it != shellSessionMap.end() && it->second) {
                                it->second->resize(rows, cols);
                            }
                        } else if (type == "shell_kill") {
                            std::string sigName = msg.value("signal", "SIGINT");
                            int sig = SIGINT;
                            if (sigName == "SIGTERM") sig = SIGTERM;
                            else if (sigName == "SIGKILL") sig = SIGKILL;
                            else if (sigName == "SIGHUP")  sig = SIGHUP;

                            std::lock_guard<std::mutex> lock(g_shell_mutex);
                            auto it = shellSessionMap.find(id);
                            if (it != shellSessionMap.end() && it->second) {
                                it->second->kill(sig);
                            }
                        } else if (type == "ping") {
                            json pong = {
                                {"type", "pong"},
                                {"t", msg.value("t", 0.0)}
                            };
                            try { dc->send(pong.dump()); } catch (...) {}
                        }
                    }
                } catch (const std::exception &e) {
                    std::cerr << "Failed to parse JSON from " << id << ": "
                              << e.what() << std::endl;
                }
            }
        });

        dataChannelMap.emplace(id, dc);
    });

    peerConnectionMap.emplace(id, pc);
    return pc;
}

// =============================================================================
// Helper functions
// =============================================================================
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

rtc::Configuration createIceConfig() {
    rtc::Configuration config;

    std::string stunServer = "";
    if (g_config->getAsBool("noStun")) {
        std::cout << "No STUN server is configured." << std::endl;
    } else {
        std::string stunHost = g_config->get("stunServer", "stun.l.google.com");
        int stunPort = g_config->getAsInt("stunPort", 19302);
        if (stunHost.substr(0, 5).compare("stun:") != 0) {
            stunServer = "stun:";
        }
        stunServer += stunHost + ":" + std::to_string(stunPort);
        std::cout << "STUN server is " << stunServer << std::endl;
        config.iceServers.emplace_back(stunServer);
    }

    std::string turnServer = g_config->get("turnServer");
    if (!turnServer.empty()) {
        std::string turnUser = g_config->get("turnUser");
        std::string turnPass = g_config->get("turnPass");
        int turnPort = g_config->getAsInt("turnPort", 3478);

        std::cout << "TURN server is " << turnServer << ":" << turnPort << std::endl;

        config.iceServers.push_back(
            rtc::IceServer(turnServer, turnPort, turnUser, turnPass,
                           rtc::IceServer::RelayType::TurnUdp));
    }

    return config;
}

// =============================================================================
// WebSocket callbacks
// =============================================================================
void setupWebSocketCallbacks(std::shared_ptr<rtc::WebSocket> ws,
                             std::shared_ptr<std::promise<void>> wsPromise) {
    auto config = createIceConfig();
    auto wws = make_weak_ptr(ws);

    ws->onOpen([wsPromise, ws]() {
        std::cout << "WebSocket connected, signaling ready" << std::endl;
        if (g_current_ws && g_current_ws != ws) {
            std::cout << "Replacing old WebSocket" << std::endl;
            g_current_ws->close();
        }
        g_current_ws = ws;
        stopWsReconnect();
        if (!g_ws_heartbeat_running.load()) {
            startWsHeartbeat();
        }
        g_last_ws_activity.store(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        wsPromise->set_value();
    });

    ws->onError([wsPromise](std::string s) {
        std::cout << "WebSocket error: " << s << std::endl;
        wsPromise->set_exception(std::make_exception_ptr(std::runtime_error(s)));
    });

    ws->onClosed([]() {
        std::cout << "WebSocket closed" << std::endl;
        startWsReconnect();
    });

    ws->onMessage([config, wws](auto data) {
        if (g_shutdown_requested.load()) {
            return;
        }

        g_last_ws_activity.store(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );

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

        if (type == "ping") {
            return;
        }

        if (type == "peer_close") {
            std::cout << "Received peer_close from " << id << std::endl;

            {
                std::lock_guard<std::mutex> lock(g_shell_mutex);
                shellSessionMap.erase(id);
            }
            dataChannelMap.erase(id);
            auto jt = peerConnectionMap.find(id);
            if (jt != peerConnectionMap.end()) {
                peerConnectionMap.erase(jt);
            }

            if (dataChannelMap.empty()) {
                std::cout << "No active data channels, shutting down..." << std::endl;
                g_shutdown_requested.store(true);
            }
            return;
        }

        shared_ptr<rtc::PeerConnection> pc;
        if (auto jt = peerConnectionMap.find(id); jt != peerConnectionMap.end()) {
            if (type == "offer") {
                std::cout << "Release old pc for peer: " << id << std::endl;
                dataChannelMap[id].reset();
                peerConnectionMap[id].reset();
                peerConnectionMap.erase(id);
                dataChannelMap.erase(id);
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

// =============================================================================
// WebSocket auto-reconnect
// =============================================================================
void startWsReconnect() {
    if (g_ws_reconnect_running.exchange(true)) {
        std::cout << "WebSocket reconnect already running" << std::endl;
        return;
    }

    std::cout << "Starting WebSocket auto-reconnect..." << std::endl;

    g_ws_reconnect_thread = std::make_shared<std::thread>([]() {
        std::atomic<bool> reconnecting(true);

        while (g_ws_reconnect_running && reconnecting) {
            try {
                auto newWs = std::make_shared<rtc::WebSocket>();
                auto wsPromise = std::make_shared<std::promise<void>>();
                auto wsFuture = wsPromise->get_future();

                setupWebSocketCallbacks(newWs, wsPromise);

                std::string wsHost = g_config->get("webSocketServer", "localhost");
                int wsPort = g_config->getAsInt("webSocketPort", 8000);
                const std::string wsPrefix = wsHost.find("://") == std::string::npos ? "ws://" : "";
                const std::string url = wsPrefix + wsHost + ":" + std::to_string(wsPort) + "/" + g_client_id;

                std::cout << "Attempting to reconnect to: " << url << std::endl;
                newWs->open(url);

                if (wsFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
                    try {
                        wsFuture.get();
                        std::cout << "WebSocket reconnection successful!" << std::endl;
                        reconnecting = false;
                    } catch (const std::exception &e) {
                        std::cerr << "WebSocket reconnection failed: " << e.what() << std::endl;
                    }
                } else {
                    std::cout << "WebSocket reconnection timeout, retrying in 3 seconds..." << std::endl;
                }
            } catch (const std::exception &e) {
                std::cerr << "WebSocket reconnect error: " << e.what() << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        g_ws_reconnect_running = false;
        std::cout << "WebSocket reconnect thread finished" << std::endl;
    });

    g_ws_reconnect_thread->detach();
}

void stopWsReconnect() {
    g_ws_reconnect_running = false;
    if (g_ws_reconnect_thread) {
        g_ws_reconnect_thread.reset();
        std::cout << "WebSocket reconnect thread stopped" << std::endl;
    }
}

// =============================================================================
// WebSocket heartbeat
// =============================================================================
void startWsHeartbeat() {
    if (g_ws_heartbeat_running.exchange(true)) {
        std::cout << "WebSocket heartbeat already running" << std::endl;
        return;
    }

    std::cout << "Starting WebSocket heartbeat monitor..." << std::endl;

    g_ws_heartbeat_thread = std::make_shared<std::thread>([]() {
        while (g_ws_heartbeat_running) {
            try {
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();

                auto last_activity = g_last_ws_activity.load();
                auto elapsed = now - last_activity;

                if (elapsed >= WS_TIMEOUT_SECONDS) {
                    std::cerr << "WebSocket connection timeout! No activity for " << elapsed
                              << " seconds" << std::endl;
                    std::cerr << "Triggering reconnection..." << std::endl;

                    if (g_current_ws) {
                        g_current_ws.reset();
                    }

                    g_ws_heartbeat_running = false;
                    startWsReconnect();
                    break;
                } else if (elapsed >= WS_HEARTBEAT_INTERVAL) {
                    if (g_current_ws) {
                        try {
                            json heartbeat = {
                                {"id", g_client_id},
                                {"type", "ping"},
                                {"timestamp", now}
                            };
                            g_current_ws->send(heartbeat.dump());
                        } catch (const std::exception &e) {
                            std::cerr << "Failed to send heartbeat: " << e.what() << std::endl;
                            g_ws_heartbeat_running = false;
                            startWsReconnect();
                            break;
                        }
                    }
                }

                std::this_thread::sleep_for(std::chrono::seconds(1));
            } catch (const std::exception &e) {
                std::cerr << "Heartbeat monitor error: " << e.what() << std::endl;
            }
        }
    });

    g_ws_heartbeat_thread->detach();
}

void stopWsHeartbeat() {
    g_ws_heartbeat_running = false;
    if (g_ws_heartbeat_thread && g_ws_heartbeat_thread->joinable()) {
        std::cout << "Stopping WebSocket heartbeat thread..." << std::endl;
        g_ws_heartbeat_thread->join();
        g_ws_heartbeat_thread.reset();
        std::cout << "WebSocket heartbeat thread stopped" << std::endl;
    }
}
