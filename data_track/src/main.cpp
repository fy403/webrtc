
#include "rtc/rtc.hpp"
#include "parse_cl.h"
#include "rc_client.h"
#include "rc_client_config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <csignal> // 用于信号处理
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>

using namespace std::chrono_literals;
using std::shared_ptr;
using std::weak_ptr;

// 全局原子标志位，用于信号处理
std::atomic<bool> g_shutdown_requested{false};

// 信号处理函数
void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down gracefully..."
            << std::endl;
    g_shutdown_requested.store(true);
}

// 设置信号处理器
void setup_signal_handlers() {
    std::signal(SIGINT, signal_handler); // Ctrl+C
    std::signal(SIGTERM, signal_handler); // 终止信号
#ifdef SIGPIPE
    // 忽略 SIGPIPE 信号，防止网络连接断开时程序异常退出
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

template<class T>
weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }

using nlohmann::json;

std::unordered_map<std::string, shared_ptr<rtc::PeerConnection> > peerConnectionMap;
std::unordered_map<std::string, shared_ptr<rtc::DataChannel> > dataChannelMap;

shared_ptr<rtc::PeerConnection> createPeerConnection(
    const rtc::Configuration &config,
    weak_ptr<rtc::WebSocket> wws,
    std::string id,
    std::shared_ptr<RCClient> client);

std::string randomId(size_t length);

int main(int argc, char **argv) {
    try {
        setup_signal_handlers(); // 设置信号处理器

        Cmdline params(argc, argv);

        // Use command line parameters or generate random ID
        std::string client_id = params.clientId(); // Use new client_id parameter
        if (client_id.empty()) {
            client_id = randomId(4);
            std::cout << "Generated client ID: " << client_id << std::endl;
        } else {
            std::cout << "Using specified client ID: " << client_id << std::endl;
        }
        // 构建 RCClient 配置对象
        RCClientConfig rcClientConfig(
            params.usbDevice(), // MotorController.MotorDriver: 串口设备
            params.motorDriverType(), // MotorController.MotorDriver: 驱动类型
            params.ttyBaudrate(), // MotorController.MotorDriver: 串口波特率
            params.gsmPort(), // SystemMonitor: 4G模块串口设备
            params.gsmBaudrate() // SystemMonitor: 4G模块串口波特率
        );

        // 创建局部的 RCClient 实例，使用智能指针管理
        std::shared_ptr<RCClient> client = std::make_shared<RCClient>(rcClientConfig);
        client->stopAll();
        // rtc 初始化
        rtc::InitLogger(rtc::LogLevel::Info);
        rtc::Configuration config;
        std::string stunServer = "";
        if (params.noStun()) {
            std::cout << "No STUN server is configured. Only local hosts and public IP "
                    "addresses supported."
                    << std::endl;
        } else {
            if (params.stunServer().substr(0, 5).compare("stun:") != 0) {
                stunServer = "stun:";
            }
            stunServer += params.stunServer() + ":" + std::to_string(params.stunPort());
            std::cout << "STUN server is " << stunServer << std::endl;
            config.iceServers.emplace_back(stunServer);
        }

        // 添加 TURN 服务器配置支持
        std::string turnServer = params.turnServer();
        if (!turnServer.empty()) {
            std::string turnUser = params.turnUser();
            std::string turnPass = params.turnPass();
            int turnPort = params.turnPort();

            std::cout << "TURN server is " << turnServer << ":" << turnPort
                    << std::endl;

            // TURN 服务器 - 使用带参数的构造函数
            config.iceServers.push_back(
                rtc::IceServer(turnServer, // hostname
                               turnPort, // port
                               turnUser, // username
                               turnPass, // password
                               rtc::IceServer::RelayType::TurnUdp // relay type
                ));
        }

        // 如果收到关闭信号，则退出
        if (g_shutdown_requested.load()) {
            std::cout << "Shutdown requested before WebSocket connection established"
                    << std::endl;
            return 0;
        }

        if (params.udpMux()) {
            std::cout << "ICE UDP mux enabled" << std::endl;
            config.enableIceUdpMux = true;
        }

        auto ws = std::make_shared<rtc::WebSocket>();

        std::promise<void> wsPromise;
        auto wsFuture = wsPromise.get_future();

        ws->onOpen([&wsPromise]() {
            std::cout << "WebSocket connected, signaling ready" << std::endl;
            wsPromise.set_value();
        });

        ws->onError([&wsPromise](std::string s) {
            std::cout << "WebSocket error" << std::endl;
            wsPromise.set_exception(std::make_exception_ptr(std::runtime_error(s)));
        });

        ws->onClosed([client]() {
            std::cout << "WebSocket closed" << std::endl;
            client->stopAll();
            throw std::runtime_error("WebSocket closed");
        });

        ws->onMessage([&config, client, wws = make_weak_ptr(ws)](auto data) {
            // 如果收到关闭信号，则忽略新消息
            if (g_shutdown_requested.load()) {
                return;
            }

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

            // Handle peer_close message
            if (type == "peer_close") {
                std::cout << "Received peer_close from " << id << ", stopping all and exiting" << std::endl;
                client->stopAll();
                throw std::runtime_error("Peer closed connection");
            }

            shared_ptr<rtc::PeerConnection> pc;
            if (auto jt = peerConnectionMap.find(id); jt != peerConnectionMap.end()) {
                if (type == "offer") {
                    std::cout << "Release old pc" << std::endl;
                    dataChannelMap[id].reset();
                    peerConnectionMap[id].reset();
                    peerConnectionMap.erase(id);
                    dataChannelMap.erase(id);
                    std::cout << "Answering to " + id << std::endl;
                    pc = createPeerConnection(config, wws, id, client);
                } else {
                    pc = jt->second;
                }
            } else if (type == "offer") {
                std::cout << "Answering to " + id << std::endl;
                pc = createPeerConnection(config, wws, id, client);
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

        const std::string wsPrefix =
                params.webSocketServer().find("://") == std::string::npos ? "ws://" : "";
        const std::string url = wsPrefix + params.webSocketServer() + ":" +
                                std::to_string(params.webSocketPort()) + "/" +
                                client_id;

        std::cout << "WebSocket URL is " << url << std::endl;
        ws->open(url);

        std::cout << "Waiting for signaling to be connected..." << std::endl;
        wsFuture.get();

        // 检查是否在连接建立之前收到了关闭信号
        if (g_shutdown_requested.load()) {
            throw std::runtime_error("Shutdown requested before main loop started");
        }

        // 主循环 - 处理睡眠和状态更新
        while (!g_shutdown_requested.load()) // 监听关闭信号
        {
            // 每秒钟检查一次状态更新
            std::this_thread::sleep_for(std::chrono::seconds(1));
            // 在主线程中处理系统状态更新
            client->sendSystemStatus();
        }

        std::cout << "Cleaning up..." << std::endl;
        client->stopAll();
        ws->close();
        dataChannelMap.clear();
        peerConnectionMap.clear();
        return 0;
    } catch (const std::exception &e) {
        std::cout << "Error: " << e.what() << std::endl;
        dataChannelMap.clear();
        peerConnectionMap.clear();
        return -1;
    }
}

// Create and setup a PeerConnection
shared_ptr<rtc::PeerConnection> createPeerConnection(
    const rtc::Configuration &config,
    weak_ptr<rtc::WebSocket> wws,
    std::string id,
    std::shared_ptr<RCClient> client) {
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

    pc->onDataChannel([id, client](shared_ptr<rtc::DataChannel> dc) {
        std::cout << "DataChannel from " << id << " received with label \""
                << dc->label() << "\"" << std::endl;

        dc->onOpen([id, dc, client]() {
            std::cout << "DataChannel from " << id << " open" << std::endl;
            client->setDataChannel(dc);
        });

        dc->onClosed([id, client]() {
            std::cout << "DataChannel from " << id << " closed" << std::endl;
            client->stopAll();
            client->setDataChannel(nullptr);
            dataChannelMap.erase(id);
        });

        dc->onMessage([id, client](auto data) {
            // Handle both string and binary data
            if (std::holds_alternative<rtc::binary>(data)) {
                const rtc::binary &bin_data = std::get<rtc::binary>(data);
                client->parseFrame(
                    reinterpret_cast<const uint8_t *>(bin_data.data()),
                    bin_data.size());
            } else {
                // Text messages are currently informational only; SBUS control uses
                // binary frames.
            }
        });
        dataChannelMap.emplace(id, dc);
    });

    peerConnectionMap.emplace(id, pc);
    return pc;
};

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
