#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <string>
#include <chrono>
#include <memory>
#include <array>
#include "4g_tty.h"

class SystemMonitor
{
public:
    // 4G ttyACM0
    FourGTty gsm;

    SystemMonitor();

    // Network statistics
    bool getNetworkStats(double &rx_speed, double &tx_speed);

    // CPU usage
    bool getCPUUsage(double &cpu_usage);

    // Service status
    bool checkServiceStatus(const std::string &service_name);

private:
    // Network traffic statistics
    unsigned long long last_rx_bytes_;
    unsigned long long last_tx_bytes_;
    std::chrono::steady_clock::time_point last_net_time_;

    // CPU usage statistics
    unsigned long long last_total_jiffies_;
    unsigned long long last_work_jiffies_;
    std::chrono::steady_clock::time_point last_cpu_time_;

    // Execute external command and get output
    std::string executeCommand(const std::string &command);
};

#endif // SYSTEM_MONITOR_H