#include "../include/network_monitor.h"
#include <sstream>
#include <iostream>
#include <memory>

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
        // 使用一条命令获取所有网络接口的统计信息
        std::string command = "cat /proc/net/dev | grep -E ':(.*)' | awk '{if(NR>2) print $1,$2,$10}'";
        std::string result = executeCommand(command);

        std::istringstream iss(result);
        std::string line;
        unsigned long long total_rx = 0, total_tx = 0;

        while (std::getline(iss, line))
        {
            std::istringstream line_stream(line);
            std::string interface;
            unsigned long long rx, tx;

            line_stream >> interface >> rx >> tx;

            // 移除接口名称中的冒号
            if (!interface.empty() && interface.back() == ':')
            {
                interface.pop_back();
            }

            // 排除回环和虚拟接口
            if (interface != "lo" && interface.find("virbr") == std::string::npos)
            {
                total_rx += rx;
                total_tx += tx;
            }
        }

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
