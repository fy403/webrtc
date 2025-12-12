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
    SystemMonitor();

    // 初始化4G模块（可选）
    bool open_4g(const std::string &gsm_port = "/dev/ttyACM0", int gsm_baudrate = 115200);

    // Network statistics
    bool getNetworkStats(double &rx_speed, double &tx_speed);

    // CPU usage
    bool getCPUUsage(double &cpu_usage);

    // Service status
    bool checkServiceStatus(const std::string &service_name);

    // 一次性获取所有GSM/4G模块信息（需要先调用 open_4g）
    void getGsmInfo(std::string &signal, std::string &simStatus, 
        std::string &network, std::string &moduleInfo, 
        int timeout_ms = 3000);
        
    // 检查4G模块是否已初始化
    bool is4gInitialized() const { return gsm_initialized_; }
        
private:
    // 4G module (GSM) - 私有成员，通过公开函数访问
    FourGTty gsm_;
    bool gsm_initialized_ = false;

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

    // GSM/4G module information accessors
    std::string getSignalQuality(int timeout_ms = 3000);
    std::string getSimStatus(int timeout_ms = 3000);
    std::string getNetworkRegistration(int timeout_ms = 3000);
    std::string getModuleInfo(int timeout_ms = 3000);
};

#endif // SYSTEM_MONITOR_H