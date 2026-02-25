#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

#include <string>
#include <chrono>
#include <array>
#include "async_monitor.h"

class NetworkMonitor : public AsyncMonitor
{
public:
    NetworkMonitor();
    ~NetworkMonitor();

    // 获取网络统计信息（下载速度和上传速度，单位：KB/s）
    bool getStats(double &rx_speed, double &tx_speed);

protected:
    // 实现基类的采集方法
    void collectData() override;

private:
    // 执行系统命令并获取输出
    std::string executeCommand(const std::string &command);

    // 内部采集方法（不使用锁，用于线程内部调用）
    bool getStatsInternal(double &rx_speed, double &tx_speed);

    // 网络流量统计
    unsigned long long last_rx_bytes_;
    unsigned long long last_tx_bytes_;
    std::chrono::steady_clock::time_point last_net_time_;

    // 缓存的采集结果
    double cached_rx_speed_;
    double cached_tx_speed_;
};

#endif // NETWORK_MONITOR_H
