#ifndef RC_CLIENT_H
#define RC_CLIENT_H

#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include <map>
#include <memory>
#include "motor_controller.h"
#include "system_monitor.h"
#include "message_handler.h"
#include "constants.h"
#include "rtc/rtc.hpp"
#include "rc_client_config.h"

class RCClient {
public:
    /**
     * 构造函数 - 使用配置类
     * @param config RCClient 系统配置，包含所有子组件的配置参数
     */
    explicit RCClient(const RCClientConfig &config = RCClientConfig());

    ~RCClient();

    void stopAll();

    void parseFrame(const uint8_t *frame, size_t length);

    void sendSystemStatus();

    void setDataChannel(std::shared_ptr<rtc::DataChannel> dc);

    std::shared_ptr<rtc::DataChannel> getDataChannel();

private:
    MotorController *motor_controller_;
    std::atomic<bool> has_timeout_;
    std::chrono::steady_clock::time_point last_heartbeat_;

    // Data channel for WebRTC communication
    std::shared_ptr<rtc::DataChannel> data_channel_;

    // System monitor
    SystemMonitor system_monitor_;
    std::chrono::steady_clock::time_point last_status_time_;

    // Message handler
    MessageHandler message_handler_;

    void sendStatusFrame(const std::map<std::string, std::string> &statusData);
};

void signalHandler(int signal);

#endif // RC_CLIENT_H