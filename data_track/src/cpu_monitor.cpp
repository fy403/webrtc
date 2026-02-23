#include "../include/cpu_monitor.h"
#include <iostream>
#include <memory>
#include <algorithm>

CPUMonitor::CPUMonitor() : last_total_jiffies_(0), last_work_jiffies_(0) {}
CPUMonitor::~CPUMonitor() {}

bool CPUMonitor::getUsage(double &cpu_usage)
{
    try
    {
        // 使用top命令获取CPU使用率
        std::string command = "top -bn1 | grep 'Cpu(s)' | awk '{print $2}' | cut -d'%' -f1";
        std::string result = executeCommand(command);

        if (!result.empty())
        {
            // 移除换行符
            result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
            cpu_usage = std::stod(result);
            return true;
        }

        // 备用方法：使用/proc/stat
        command = "grep 'cpu ' /proc/stat | awk '{print ($2+$4)*100/($2+$4+$5)}'";
        result = executeCommand(command);

        if (!result.empty())
        {
            result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
            cpu_usage = std::stod(result);
            return true;
        }

        cpu_usage = 0;
        return false;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to get CPU usage: " << e.what() << std::endl;
        cpu_usage = 0;
        return false;
    }
}

std::string CPUMonitor::executeCommand(const std::string &command)
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
