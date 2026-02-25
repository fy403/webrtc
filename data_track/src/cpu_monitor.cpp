#include "../include/cpu_monitor.h"
#include <iostream>
#include <memory>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cmath>

CPUMonitor::CPUMonitor()
    : AsyncMonitor(),
      last_total_jiffies_(0), last_work_jiffies_(0),
      cached_cpu_usage_(0)
{
    cached_cpu_info_.core_count = 0;
    cached_cpu_info_.total_usage = 0;
    cached_cpu_info_.total_tasks = 0;
    cached_cpu_info_.running_tasks = 0;
    cached_cpu_info_.sleeping_tasks = 0;
    cached_cpu_info_.load_1min = 0;
    cached_cpu_info_.load_5min = 0;
    cached_cpu_info_.load_15min = 0;
}

CPUMonitor::~CPUMonitor()
{
}

void CPUMonitor::collectData()
{
    double cpu_usage;
    if (getUsageInternal(cpu_usage))
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        cached_cpu_usage_ = cpu_usage;
    }

    CPUInfo info;
    if (getCPUInfoInternal(info))
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        cached_cpu_info_ = info;
    }
}

bool CPUMonitor::getUsage(double &cpu_usage)
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    cpu_usage = cached_cpu_usage_;
    return true;
}

bool CPUMonitor::getCPUInfo(CPUInfo &info)
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    info = cached_cpu_info_;
    return true;
}

bool CPUMonitor::getUsageInternal(double &cpu_usage)
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

bool CPUMonitor::getCPUInfoInternal(CPUInfo &info)
{
    try
    {
        // 读取/proc/stat获取CPU核心数和各核心状态
        std::ifstream stat_file("/proc/stat");
        if (!stat_file.is_open())
        {
            return false;
        }

        std::string line;
        int core_count = 0;
        std::vector<unsigned long long> core_totals, core_works;

        while (std::getline(stat_file, line))
        {
            if (line.substr(0, 3) == "cpu" && line[3] != ' ')
            {
                // 这是CPU核心行，格式：cpuN user nice system idle iowait irq softirq
                std::istringstream iss(line);
                std::string cpu_label;
                unsigned long long user, nice, system, idle, iowait, irq, softirq;

                iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq;

                unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
                unsigned long long work = user + nice + system;

                core_totals.push_back(total);
                core_works.push_back(work);
                core_count++;
            }
        }
        stat_file.close();

        info.core_count = core_count;

        // 计算各核心使用率
        if (!last_core_total_jiffies_.empty() && last_core_total_jiffies_.size() == core_count)
        {
            info.core_usage.resize(core_count);
            for (int i = 0; i < core_count; ++i)
            {
                unsigned long long total_diff = core_totals[i] - last_core_total_jiffies_[i];
                unsigned long long work_diff = core_works[i] - last_core_work_jiffies_[i];
                info.core_usage[i] = (total_diff > 0) ? (work_diff * 100.0 / total_diff) : 0.0;
            }
        }
        else
        {
            info.core_usage.resize(core_count, 0.0);
        }

        last_core_total_jiffies_ = core_totals;
        last_core_work_jiffies_ = core_works;

        // 读取/proc/stat总CPU使用率
        std::string command = "grep '^cpu ' /proc/stat | awk '{print $2+$3+$4+$5+$6+$7+$8, $2+$4}'";
        std::string result = executeCommand(command);
        if (!result.empty())
        {
            std::istringstream iss(result);
            unsigned long long total_jiffies, work_jiffies;
            iss >> total_jiffies >> work_jiffies;

            if (last_total_jiffies_ > 0 && total_jiffies > last_total_jiffies_)
            {
                info.total_usage = (work_jiffies - last_work_jiffies_) * 100.0 /
                                   (total_jiffies - last_total_jiffies_);
            }
            else
            {
                info.total_usage = 0;
            }
            last_total_jiffies_ = total_jiffies;
            last_work_jiffies_ = work_jiffies;
        }

        // 读取/proc/loadavg获取负载
        std::ifstream loadavg_file("/proc/loadavg");
        if (loadavg_file.is_open())
        {
            loadavg_file >> info.load_1min >> info.load_5min >> info.load_15min;

            // 读取任务数信息：格式：running/tasks total_tasks
            int running, total;
            char slash;
            loadavg_file >> running >> slash >> total;
            info.running_tasks = running;
            info.total_tasks = total;

            loadavg_file.close();
        }

        // 计算睡眠任务数
        info.sleeping_tasks = info.total_tasks - info.running_tasks;

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to get CPU info: " << e.what() << std::endl;
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
