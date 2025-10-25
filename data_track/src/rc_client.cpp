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

RCClient::RCClient(const std::string &tty_port)
    : running_(false), has_timeout_(false), heartbeat_timeout_(3.0),
      motor_controller_(new MotorControllerTTY(tty_port)), data_channel_(nullptr)
{
    last_heartbeat_ = std::chrono::steady_clock::now();
    last_status_time_ = std::chrono::steady_clock::now();
    // resolve_hostname(); // Implementation needed
}

RCClient::~RCClient()
{
    disconnect();
    if (motor_controller_)
    {
        delete motor_controller_;
    }
}

void RCClient::setDataChannel(std::shared_ptr<rtc::DataChannel> dc)
{
    data_channel_ = dc;
}
std::shared_ptr<rtc::DataChannel> RCClient::getDataChannel()
{
    return data_channel_;
}

void RCClient::run()
{
    // Implementation needed
    std::cout << "RCClient running..." << std::endl;
}

void RCClient::stop()
{
    running_ = false;
}

void RCClient::parseFrame(const uint8_t *frame)
{
    auto parsed = message_handler_.parseFrame(frame);
    if (!parsed.valid)
    {
        return;
    }

    uint8_t msg_type = parsed.message_type;
    uint8_t key_code = parsed.key_code;
    uint8_t value = parsed.value;

    if (msg_type == MSG_PING)
    {
        last_heartbeat_ = std::chrono::steady_clock::now();
        has_timeout_ = false;
        sendSystemStatus();
        return;
    }

    if (msg_type == MSG_KEY)
    {
        if (key_code >= 1 && key_code <= 4)
        {
            bool desired = (value != 0);
            std::cout << "Key event: " << KEY_NAMES[key_code - 1] << " -> "
                      << (desired ? "pressed" : "released") << std::endl;
            motor_controller_->setKeyState(KEY_NAMES[key_code - 1], desired);
        }
        return;
    }

    if (msg_type == MSG_EMERGENCY_STOP)
    {
        motor_controller_->emergencyStop();
        return;
    }

    if (msg_type == MSG_CYCLE_THROTTLE)
    {
        motor_controller_->cycleThrottle();
        return;
    }

    if (msg_type == MSG_STOP_ALL)
    {
        motor_controller_->stopAll();
        return;
    }

    if (msg_type == MSG_QUIT)
    {
        running_ = false;
        motor_controller_->emergencyStop();
        std::cout << "Received quit command" << std::endl;
        return;
    }
}

void RCClient::sendStatusFrame(const std::map<std::string, std::string> &statusData)
{
    std::vector<uint8_t> frame;
    message_handler_.createStatusFrame(statusData, frame);

    // Send data through RTC data channel if available
    if (data_channel_)
    {
        data_channel_->send(reinterpret_cast<const std::byte *>(frame.data()), frame.size());
    }
}

void RCClient::sendSystemStatus()
{
    double rx_speed, tx_speed, cpu_usage;

    bool net_ok = system_monitor_.getNetworkStats(rx_speed, tx_speed);
    bool cpu_ok = system_monitor_.getCPUUsage(cpu_usage);

    bool tty_service = system_monitor_.checkServiceStatus("data_track_rtc.service");
    bool rtsp_service = system_monitor_.checkServiceStatus("av_track_rtc.service");

    std::string signal = system_monitor_.gsm.getSignalQuality();
    std::string simStatus = system_monitor_.gsm.getSimStatus();
    std::string network = system_monitor_.gsm.getNetworkRegistration();
    std::string moduleInfo = system_monitor_.gsm.getModuleInfo();

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

void RCClient::heartbeatCheck()
{
    has_timeout_ = false;

    while (running_)
    {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_heartbeat_).count();

        if (elapsed > heartbeat_timeout_ && !has_timeout_)
        {
            std::cout << "Heartbeat timeout, executing emergency stop" << std::endl;
            motor_controller_->emergencyStop();
            has_timeout_ = true;
            exit(2);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

void RCClient::resolve_hostname()
{
    // Implementation needed
}

void RCClient::disconnect()
{
    running_ = false;
}
