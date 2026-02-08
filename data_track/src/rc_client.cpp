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
      data_channel_(nullptr),
      // SystemMonitor 不需要在构造函数中初始化4G模块
      system_monitor_(),
      watchdog_running_(true),
      watchdog_timeout_ms_(config.watchdog_timeout_ms),
      watchdog_check_interval_ms_(50),
      heartbeat_count_(0),
      total_heartbeat_interval_ms_(0) {
    last_heartbeat_ = std::chrono::steady_clock::now();
    last_heartbeat_time_ = std::chrono::steady_clock::now();
    last_status_time_ = std::chrono::steady_clock::now();

    // 如果配置中指定了4G模块参数，则初始化4G模块
    if (config.has4gConfig()) {
        if (!system_monitor_.open_4g(config.system_monitor_gsm_port, config.system_monitor_gsm_baudrate)) {
            std::cerr << "Warning: Failed to initialize 4G module, continuing without 4G support" << std::endl;
        }
    }

    // 启动watchdog线程
    watchdog_thread_ = std::thread(&RCClient::watchdogLoop, this);
    std::cout << "Watchdog保护已启动，初始超时时间: " << watchdog_timeout_ms_ << "ms, 检查间隔: " << watchdog_check_interval_ms_ << "ms" << std::endl;
}

RCClient::~RCClient() {
    // 停止watchdog线程
    watchdog_running_ = false;
    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }

    if (motor_controller_) {
        delete motor_controller_;
    }
}

void RCClient::setDataChannel(std::shared_ptr<rtc::DataChannel> dc) {
    data_channel_ = dc;
}

std::shared_ptr<rtc::DataChannel> RCClient::getDataChannel() {
    return data_channel_;
}

void RCClient::parseFrame(const uint8_t *frame, size_t length) {
    // 解析 RC Protocol v2
    RCProtocolV2::ControlFrame control_frame;

    if (!RCProtocolV2::parseControlFrame(frame, length, control_frame)) {
        std::cerr << "Invalid RC v2 frame received" << std::endl;
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // 更新watchdog参数（统计心跳间隔并动态调整）
    updateWatchdogParams();

    last_heartbeat_ = now;
    last_heartbeat_time_ = now;

    // 直接传递控制帧给电机控制器
    motor_controller_->applyControl(control_frame);
}

void RCClient::sendStatusFrame(const std::map<std::string, std::string> &statusData) {
    // Convert to JSON
    json j;
    for (const auto &pair : statusData) {
        j[pair.first] = pair.second;
    }

    // Convert JSON to string and send directly
    std::string jsonStr = j.dump();

    // Send data through RTC data channel if available
    if (data_channel_) {
        data_channel_->send(reinterpret_cast<const std::byte *>(jsonStr.data()), jsonStr.size());
    } else {
        // std::cout << "Sending status frame faild: data_channel is down!" << std::endl;
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

void RCClient::updateWatchdogParams() {
    auto now = std::chrono::steady_clock::now();

    // 统计心跳间隔
    if (heartbeat_count_ > 0) {
        auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_).count();
        total_heartbeat_interval_ms_ += interval;
        heartbeat_count_++;

        // 计算平均心跳间隔
        int64_t avg_interval_ms = total_heartbeat_interval_ms_ / heartbeat_count_;

        // 动态调整检查间隔和超时时间
        // 检查间隔设置为平均间隔的20%，但不少于10ms，不大于100ms
        int new_check_interval = std::max(10, std::min(100, static_cast<int>(avg_interval_ms * 0.2)));
        // 超时时间设置为平均间隔的3倍，但不少于300ms
        int new_timeout = std::max(300, static_cast<int>(avg_interval_ms * 3));

        if (new_check_interval != watchdog_check_interval_ms_ || new_timeout != watchdog_timeout_ms_) {
            watchdog_check_interval_ms_ = new_check_interval;
            watchdog_timeout_ms_ = new_timeout;
            std::cout << "动态调整: 平均心跳间隔=" << avg_interval_ms << "ms, 检查间隔=" << watchdog_check_interval_ms_
                      << "ms, 超时时间=" << watchdog_timeout_ms_ << "ms" << std::endl;
        }
    } else {
        heartbeat_count_++;
    }
}

void RCClient::watchdogLoop() {
    while (watchdog_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(watchdog_check_interval_ms_));

        if (!watchdog_running_) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_).count();

        if (has_timeout_) {
            has_timeout_ = false;
            motor_controller_->stopAll();
            continue;
        }

        if (elapsed > watchdog_timeout_ms_) {
            //            std::cout << "Watchdog超时检测: " << elapsed << "ms未收到控制命令，自动停止电机" << std::endl;
            motor_controller_->stopAll();
        }
    }
}

