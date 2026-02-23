#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

#include <string>
#include <chrono>
#include <array>

class NetworkMonitor
{
public:
    NetworkMonitor();
    ~NetworkMonitor();

    // 获取网络统计信息（下载速度和上传速度，单位：KB/s）
    bool getStats(double &rx_speed, double &tx_speed);

private:
    // 执行系统命令并获取输出
    std::string executeCommand(const std::string &command);

    // 网络流量统计
    unsigned long long last_rx_bytes_;
    unsigned long long last_tx_bytes_;
    std::chrono::steady_clock::time_point last_net_time_;
};

#endif // NETWORK_MONITOR_H
