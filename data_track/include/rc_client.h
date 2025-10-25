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

class RCClient
{
public:
    RCClient(const std::string &tty_port = "/dev/ttyUSB0");
    ~RCClient();

    void run();
    void stop();
    void parseFrame(const uint8_t *frame);
    void setDataChannel(std::shared_ptr<rtc::DataChannel> dc);
    std::shared_ptr<rtc::DataChannel> getDataChannel();

private:
    MotorControllerTTY *motor_controller_;
    std::atomic<bool> running_;
    std::atomic<bool> has_timeout_;
    double heartbeat_timeout_;
    std::chrono::steady_clock::time_point last_heartbeat_;

    // Data channel for WebRTC communication
    std::shared_ptr<rtc::DataChannel> data_channel_;

    // System monitor
    SystemMonitor system_monitor_;
    std::chrono::steady_clock::time_point last_status_time_;

    // Message handler
    MessageHandler message_handler_;

    // Message types are defined in constants.h

    void sendStatusFrame(const std::map<std::string, std::string> &statusData);
    void sendSystemStatus();
    void heartbeatCheck();
    void resolve_hostname();
    void disconnect();
};

void signalHandler(int signal);

#endif // RC_CLIENT_H