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

// Global variable for signal handling
extern RCClient *global_client;

RCClient::RCClient(const RCClientConfig &config)
        : has_timeout_(false),
        // 使用配置类中的 MotorController 配置
          motor_controller_(new MotorController(config.motor_controller_config)),
          data_channel_(nullptr),
        // SystemMonitor 不需要在构造函数中初始化4G模块
          system_monitor_(),
          watchdog_running_(true),
          watchdog_timeout_ms_(config.watchdog_timeout_ms) {
    last_heartbeat_ = std::chrono::steady_clock::now();
    last_status_time_ = std::chrono::steady_clock::now();

    // 如果配置中指定了4G模块参数，则初始化4G模块
    if (config.has4gConfig()) {
        if (!system_monitor_.open_4g(config.system_monitor_gsm_port, config.system_monitor_gsm_baudrate)) {
            std::cerr << "Warning: Failed to initialize 4G module, continuing without 4G support" << std::endl;
        }
    }

    // 启动watchdog线程
    watchdog_thread_ = std::thread(&RCClient::watchdogLoop, this);
    std::cout << "Watchdog保护已启动，超时时间: " << watchdog_timeout_ms_ << "ms" << std::endl;
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
    MessageHandler::SbusFrame sbus_frame;
    if (!message_handler_.parseSbusFrame(frame, length, sbus_frame)) {
        std::cerr << "Invalid SBUS frame received" << std::endl;
        return;
    }

    last_heartbeat_ = std::chrono::steady_clock::now();

    // Channel 0 -> forward/backward, Channel 1 -> left/right
    const double forward = MessageHandler::sbusToNormalized(sbus_frame.channels[0]);
    const double turn = MessageHandler::sbusToNormalized(sbus_frame.channels[1]);
    motor_controller_->applySbus(forward, turn);

    if (sbus_frame.failsafe) {
        std::cout << "SBUS failsafe detected, triggering emergency stop" << std::endl;
        motor_controller_->emergencyStop();
        has_timeout_ = true;
    }
}

void RCClient::sendStatusFrame(const std::map<std::string, std::string> &statusData) {
    std::vector<uint8_t> frame;
    message_handler_.createStatusFrame(statusData, frame);

    // Send data through RTC data channel if available
    if (data_channel_) {
        data_channel_->send(reinterpret_cast<const std::byte *>(frame.data()), frame.size());
    } else {
        std::cout << "Sending status frame faild: data_channel is down!" << std::endl;
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

void RCClient::watchdogLoop() {
    while (watchdog_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 每50ms检查一次

        if (!watchdog_running_) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_).count();

        if (has_timeout_) {
            has_timeout_ = false;
//            std::cout << "SBUS failsafe detected, triggering emergency stop" << std::endl;
            motor_controller_->stopAll();
            continue;
        }

        if (elapsed > watchdog_timeout_ms_) {
//            std::cout << "Watchdog超时检测: " << elapsed << "ms未收到控制命令，自动停止电机" << std::endl;
            motor_controller_->stopAll();
        }
    }
}

