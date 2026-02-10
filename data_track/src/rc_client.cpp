#include "rc_client.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// RCClient is now a local variable in main(), managed by unique_ptr

RCClient::RCClient(const RCClientConfig &config)
    : has_timeout_(false),
      // 使用配置类中的 MotorController 配置
      motor_controller_(new MotorController(config.motor_controller_config)),
      // SystemMonitor 不需要在构造函数中初始化4G模块
      system_monitor_(),
      health_check_running_(true),
      default_watchdog_timeout_ms_(config.watchdog_timeout_ms) {
    last_status_time_ = std::chrono::steady_clock::now();

    // 如果配置中指定了4G模块参数，则初始化4G模块
    if (config.has4gConfig()) {
        if (!system_monitor_.open_4g(config.system_monitor_gsm_port, config.system_monitor_gsm_baudrate)) {
            std::cerr << "Warning: Failed to initialize 4G module, continuing without 4G support" << std::endl;
        }
    }

    // 启动DataChannel健康检查线程
    health_check_thread_ = std::thread(&RCClient::healthCheckLoop, this);
    std::cout << "DataChannel健康检查已启动，检查间隔: " << HEALTH_CHECK_INTERVAL_MS << "ms" << std::endl;
}

RCClient::~RCClient() {
    // 停止健康检查线程
    health_check_running_ = false;
    if (health_check_thread_.joinable()) {
        health_check_thread_.join();
    }

    if (motor_controller_) {
        delete motor_controller_;
    }
}


void RCClient::addDataChannel(const std::string &peer_id, std::shared_ptr<rtc::DataChannel> dc) {
    if (!dc || peer_id.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(data_channels_mutex_);
    data_channels_[peer_id] = dc;

    // 初始化健康检查状态
    {
        std::lock_guard<std::mutex> health_lock(channel_health_mutex_);
        DataChannelHealth health;
        health.last_heartbeat = std::chrono::steady_clock::now();
        health.is_alive = true;
        health.missed_heartbeat_count.store(0);
        channel_health_[peer_id].last_heartbeat = health.last_heartbeat;
        channel_health_[peer_id].is_alive = health.is_alive;
        channel_health_[peer_id].missed_heartbeat_count.store(health.missed_heartbeat_count.load());
    }

    std::cout << "DataChannel added for peer: " << peer_id
            << ", total channels: " << data_channels_.size() << std::endl;
}

void RCClient::removeDataChannel(const std::string &peer_id) {
    std::lock_guard<std::mutex> lock(data_channels_mutex_);
    auto it = data_channels_.find(peer_id);
    if (it != data_channels_.end()) {
        data_channels_.erase(it);
        std::cout << "DataChannel removed for peer: " << peer_id
                << ", remaining channels: " << data_channels_.size() << std::endl;
    }

    // 移除健康检查状态
    {
        std::lock_guard<std::mutex> health_lock(channel_health_mutex_);
        channel_health_.erase(peer_id);
    }
}

size_t RCClient::getDataChannelCount() const {
    std::lock_guard<std::mutex> lock(data_channels_mutex_);
    return data_channels_.size();
}

void RCClient::sendStatusFrame(const std::map<std::string, std::string> &statusData) {
    // Convert to JSON
    json j;
    for (const auto &pair: statusData) {
        j[pair.first] = pair.second;
    }

    // Convert JSON to string and send directly
    std::string jsonStr = j.dump();

    // 收集失效的DataChannel peer_id
    std::vector<std::string> failed_peers;

    // 线程安全地遍历所有DataChannel并发送数据
    {
        std::lock_guard<std::mutex> lock(data_channels_mutex_);
        for (const auto &peer_channel: data_channels_) {
            const std::string &peer_id = peer_channel.first;
            const auto &dc = peer_channel.second;

            if (dc && dc->isOpen()) {
                try {
                    dc->send(reinterpret_cast<const std::byte *>(jsonStr.data()), jsonStr.size());
                } catch (const std::exception &e) {
                    std::cerr << "Failed to send status to peer " << peer_id << ": " << e.what() << std::endl;
                    failed_peers.push_back(peer_id);
                }
            } else {
                failed_peers.push_back(peer_id);
            }
        }
    }

    // 移除失效的DataChannel
    for (const auto &peer_id: failed_peers) {
        std::cerr << "Removing failed DataChannel: " << peer_id << std::endl;
        removeDataChannel(peer_id);
    }
}

void RCClient::stopAll() {
    motor_controller_->stopAll();
}

void RCClient::sendSystemStatus() {
    double rx_speed, tx_speed, cpu_usage;

    system_monitor_.getNetworkStats(rx_speed, tx_speed);
    system_monitor_.getCPUUsage(cpu_usage);

    bool tty_service = system_monitor_.checkServiceStatus("data_track_rtc.service");
    bool rtsp_service = system_monitor_.checkServiceStatus("av_track_rtc.service");

    std::string signal, simStatus, network, moduleInfo;
    system_monitor_.getGsmInfo(signal, simStatus, network, moduleInfo);

    std::map<std::string, std::string> statusData;
    statusData["rx_speed"] = std::to_string(static_cast<uint16_t>(rx_speed * 100));
    statusData["tx_speed"] = std::to_string(static_cast<uint16_t>(tx_speed * 100));
    // System resources
    statusData["cpu_usage"] = std::to_string(static_cast<uint16_t>(cpu_usage * 100));
    // Service status
    statusData["tty_service"] = tty_service ? "1" : "0";
    statusData["rtsp_service"] = rtsp_service ? "1" : "0";
    // 4G
    statusData["4g_signal"] = signal;
    statusData["sim_status"] = simStatus;
    statusData["network"] = network;
    statusData["module_info"] = moduleInfo;
    // System info (can be extended)
    statusData["timestamp"] = std::to_string(std::time(nullptr));
    // Send system status frame
    sendStatusFrame(statusData);
}


void RCClient::parseFrame(const std::string &peer_id, const uint8_t *frame, size_t length) {
    // 多端场景下：使用互斥锁保护parseFrame，防止并发调用导致电机控制冲突
    std::lock_guard<std::mutex> lock(parse_frame_mutex_);

    // 解析 RC Protocol v2
    RCProtocolV2::ControlFrame control_frame;

    if (!RCProtocolV2::parseControlFrame(frame, length, control_frame)) {
        std::cerr << "Invalid RC v2 frame received from peer " << peer_id << std::endl;
        return;
    }

    // 更新最后发送控制命令的peer_id
    {
        std::lock_guard<std::mutex> peer_id_lock(last_control_peer_id_mutex_);
        last_control_peer_id_ = peer_id;
    }
    // 直接传递控制帧给电机控制器
    motor_controller_->applyControl(control_frame);

    // 更新特定DataChannel的健康状态
    {
        std::lock_guard<std::mutex> health_lock(channel_health_mutex_);
        DataChannelHealth &health = channel_health_[peer_id];
        health.last_heartbeat = std::chrono::steady_clock::now();
        health.missed_heartbeat_count = 0;
        health.is_alive = true;
    }
}

void RCClient::healthCheckLoop() {
    while (health_check_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(HEALTH_CHECK_INTERVAL_MS));

        if (!health_check_running_) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        std::string last_control_peer_to_check;

        // 获取最后发送控制命令的peer_id
        {
            std::lock_guard<std::mutex> peer_id_lock(last_control_peer_id_mutex_);
            last_control_peer_to_check = last_control_peer_id_;
        }

        // 检查所有DataChannel的健康状态
        {
            std::lock_guard<std::mutex> lock(channel_health_mutex_);
            for (auto &peer_health: channel_health_) {
                const std::string &peer_id = peer_health.first;
                DataChannelHealth &health = peer_health.second;

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - health.last_heartbeat).count();

                // 检查是否超时
                if (health.is_alive && elapsed > default_watchdog_timeout_ms_) {
                    health.missed_heartbeat_count++;

                    if (health.missed_heartbeat_count >= MAX_MISSED_HEARTBEATS) {
                        health.is_alive = false;
                        std::cout << "Peer " << peer_id << " marked as unhealthy ("
                                << elapsed << "ms elapsed)" << std::endl;

                        // 如果断开的是最后控制的peer，立即停止电机
                        if (peer_id == last_control_peer_to_check) {
                            std::cout << "最后控制的peer " << peer_id << " 断开，自动停止电机" << std::endl;
                            motor_controller_->stopAll();
                        }
                    }
                }
            }
        }
    }
}

