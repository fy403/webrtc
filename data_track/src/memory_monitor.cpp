#include "../include/memory_monitor.h"
#include <iostream>
#include <memory>
#include <algorithm>
#include <sstream>
#include <iomanip>

MemoryMonitor::MemoryMonitor()
    : AsyncMonitor(),
      cached_total_mb_(0), cached_used_mb_(0),
      cached_free_mb_(0), cached_usage_percent_(0) {}

MemoryMonitor::~MemoryMonitor()
{
}

void MemoryMonitor::collectData()
{
    double total_mb, used_mb, free_mb, usage_percent;
    if (getMemoryInfoInternal(total_mb, used_mb, free_mb, usage_percent))
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        cached_total_mb_ = total_mb;
        cached_used_mb_ = used_mb;
        cached_free_mb_ = free_mb;
        cached_usage_percent_ = usage_percent;
    }
}

bool MemoryMonitor::getMemoryInfo(double &total_mb, double &used_mb, double &free_mb, double &usage_percent)
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    total_mb = cached_total_mb_;
    used_mb = cached_used_mb_;
    free_mb = cached_free_mb_;
    usage_percent = cached_usage_percent_;
    return true;
}

bool MemoryMonitor::getMemoryInfoInternal(double &total_mb, double &used_mb, double &free_mb, double &usage_percent)
{
    try
    {
        // 使用free命令获取内存信息
        std::string command = "free -m | grep '^Mem:' | awk '{print $2, $3, $4, $7}'";
        std::string result = executeCommand(command);

        std::istringstream iss(result);
        double total, used, free, available;

        if (iss >> total >> used >> free >> available)
        {
            total_mb = total;
            used_mb = used;
            free_mb = free;
            usage_percent = (total > 0) ? (used / total * 100.0) : 0.0;
            return true;
        }

        total_mb = 0;
        used_mb = 0;
        free_mb = 0;
        usage_percent = 0;
        return false;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to get memory info: " << e.what() << std::endl;
        total_mb = 0;
        used_mb = 0;
        free_mb = 0;
        usage_percent = 0;
        return false;
    }
}

std::string MemoryMonitor::executeCommand(const std::string &command)
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
