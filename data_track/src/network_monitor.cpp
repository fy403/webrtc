#include "../include/network_monitor.h"
#include <sstream>
#include <iostream>
#include <memory>
#include <fstream>
#include <string>
#include <chrono>
#include <array>
#include <cstdio>

NetworkMonitor::NetworkMonitor()
    : AsyncMonitor(),
      last_rx_bytes_(0), last_tx_bytes_(0),
      last_net_time_(std::chrono::steady_clock::now()),
      cached_rx_speed_(0), cached_tx_speed_(0) {}

NetworkMonitor::~NetworkMonitor()
{
}

void NetworkMonitor::collectData()
{
    double rx_speed, tx_speed;
    if (getStatsInternal(rx_speed, tx_speed))
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        cached_rx_speed_ = rx_speed;
        cached_tx_speed_ = tx_speed;
    }
}

bool NetworkMonitor::getStats(double &rx_speed, double &tx_speed)
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    rx_speed = cached_rx_speed_;
    tx_speed = cached_tx_speed_;
    return true;
}

bool NetworkMonitor::getStatsInternal(double &rx_speed, double &tx_speed)
{
    try
    {
        // 直接解析 /proc/net/dev，避免使用 shell 命令（更可靠）
        std::ifstream file("/proc/net/dev");
        if (!file.is_open())
        {
            std::cerr << "Failed to open /proc/net/dev" << std::endl;
            rx_speed = 0;
            tx_speed = 0;
            return false;
        }

        std::string line;
        unsigned long long total_rx = 0, total_tx = 0;
        int line_num = 0;

        while (std::getline(file, line))
        {
            line_num++;

            // 跳过前2行头部
            if (line_num <= 2)
                continue;

            // 解析接口行：格式为 "  wlan0: rx_bytes rx_packets ... tx_bytes ..."
            // 找到冒号位置，冒号前是接口名
            size_t colon_pos = line.find(':');
            if (colon_pos == std::string::npos)
                continue;

            // 提取接口名（冒号前的部分，去掉首尾空格）
            std::string interface = line.substr(0, colon_pos);
            // 去掉左侧空格
            interface.erase(0, interface.find_first_not_of(" \t"));
            // 去掉右侧空格（如果有）
            interface.erase(interface.find_last_not_of(" \t") + 1);

            // 排除回环和虚拟接口
            if (interface == "lo" || interface.find("virbr") != std::string::npos)
                continue;

            // 提取冒号后的数字部分
            std::string stats_part = line.substr(colon_pos + 1);
            std::istringstream stats_stream(stats_part);

            unsigned long long rx_bytes = 0;
            unsigned long long tx_bytes = 0;

            // /proc/net/dev 格式（空格分隔）：
            // 字段1: 接口名（含冒号）
            // 接收字段: bytes packets errs drop fifo frame compressed multicast
            // 发送字段: bytes packets errs drop fifo colls carrier compressed
            // 所以 rx_bytes 是冒号后第1个数字，tx_bytes 是第9个数字

            stats_stream >> rx_bytes;
            // 跳过 rx 的其他6个字段 (packets, errs, drop, fifo, frame, compressed, multicast)
            unsigned long long dummy;
            for (int i = 0; i < 7; i++) stats_stream >> dummy;
            // 读取 tx_bytes
            stats_stream >> tx_bytes;

            total_rx += rx_bytes;
            total_tx += tx_bytes;
        }

        file.close();

        auto now = std::chrono::steady_clock::now();
        double time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_net_time_).count() / 1000.0;

        if (last_rx_bytes_ > 0 && time_diff > 0)
        {
            rx_speed = (total_rx - last_rx_bytes_) / time_diff / 1024.0; // KB/s
            tx_speed = (total_tx - last_tx_bytes_) / time_diff / 1024.0; // KB/s
        }
        else
        {
            rx_speed = 0;
            tx_speed = 0;
        }

        last_rx_bytes_ = total_rx;
        last_tx_bytes_ = total_tx;
        last_net_time_ = now;

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to get network statistics: " << e.what() << std::endl;
        rx_speed = 0;
        tx_speed = 0;
        return false;
    }
}

std::string NetworkMonitor::executeCommand(const std::string &command)
{
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe)
    {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }
    return result;
}
