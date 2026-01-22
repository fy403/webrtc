#include "system_monitor.h"
#include <iostream>
#include <sstream>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cstring>

SystemMonitor::SystemMonitor()
{
    last_net_time_ = std::chrono::steady_clock::now();
    last_cpu_time_ = std::chrono::steady_clock::now();
    last_rx_bytes_ = 0;
    last_tx_bytes_ = 0;
    last_total_jiffies_ = 0;
    last_work_jiffies_ = 0;
    gsm_initialized_ = false;
}

// 初始化4G模块（可选）
bool SystemMonitor::open_4g(const std::string &gsm_port, int gsm_baudrate)
{
    if (gsm_initialized_) {
        std::cerr << "Warning: 4G module already initialized" << std::endl;
        return true;
    }
    
    if (gsm_.open(gsm_port, gsm_baudrate)) {
        gsm_initialized_ = true;
        return true;
    } else {
        std::cerr << "Failed to initialize 4G module on " << gsm_port << std::endl;
        return false;
    }
}

std::string SystemMonitor::executeCommand(const std::string &command)
{
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool SystemMonitor::getNetworkStats(double &rx_speed, double &tx_speed)
{
    try {
        // Use one command to get statistics for all network interfaces
        std::string command = "cat /proc/net/dev | grep -E ':(.*)' | awk '{if(NR>2) print $1,$2,$10}'";
        std::string result = executeCommand(command);

        std::istringstream iss(result);
        std::string line;
        unsigned long long total_rx = 0, total_tx = 0;

        while (std::getline(iss, line)) {
            std::istringstream line_stream(line);
            std::string interface;
            unsigned long long rx, tx;

            line_stream >> interface >> rx >> tx;

            // Remove colon from interface name
            if (!interface.empty() && interface.back() == ':') {
                interface.pop_back();
            }

            // Exclude loopback and virtual interfaces
            if (interface != "lo" && interface.find("virbr") == std::string::npos) {
                total_rx += rx;
                total_tx += tx;
            }
        }

        auto now = std::chrono::steady_clock::now();
        double time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_net_time_).count() / 1000.0;

        if (last_rx_bytes_ > 0 && time_diff > 0) {
            rx_speed = (total_rx - last_rx_bytes_) / time_diff / 1024.0; // Kbit/s
            tx_speed = (total_tx - last_tx_bytes_) / time_diff / 1024.0; // Kbit/s
        } else {
            rx_speed = 0;
            tx_speed = 0;
        }

        last_rx_bytes_ = total_rx;
        last_tx_bytes_ = total_tx;
        last_net_time_ = now;

        return true;
    } catch (const std::exception &e) {
        std::cerr << "Failed to get network statistics: " << e.what() << std::endl;
        rx_speed = 0;
        tx_speed = 0;
        return false;
    }
}

bool SystemMonitor::getCPUUsage(double &cpu_usage)
{
    try {
        // Use one command to get CPU usage
        std::string command = "top -bn1 | grep 'Cpu(s)' | awk '{print $2}' | cut -d'%' -f1";
        std::string result = executeCommand(command);

        if (!result.empty()) {
            // Remove newline characters
            result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
            cpu_usage = std::stod(result);
            return true;
        }

        // Backup method: use /proc/stat
        command = "grep 'cpu ' /proc/stat | awk '{print ($2+$4)*100/($2+$4+$5)}'";
        result = executeCommand(command);

        if (!result.empty()) {
            result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
            cpu_usage = std::stod(result);
            return true;
        }

        cpu_usage = 0;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "Failed to get CPU usage: " << e.what() << std::endl;
        cpu_usage = 0;
        return false;
    }
}

bool SystemMonitor::checkServiceStatus(const std::string &service_name)
{
    try {
        std::string command = "systemctl is-active " + service_name + " 2>/dev/null";
        std::string result = executeCommand(command);

        // Remove newline characters and check result
        result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
        return (result == "active");
    } catch (const std::exception &e) {
        std::cerr << "Failed to check service status: " << service_name << " - " << e.what() << std::endl;
        return false;
    }
}

// GSM/4G module information accessors
std::string SystemMonitor::getSignalQuality(int timeout_ms)
{
    return gsm_.getSignalQuality(timeout_ms);
}

std::string SystemMonitor::getSimStatus(int timeout_ms)
{
    return gsm_.getSimStatus(timeout_ms);
}

std::string SystemMonitor::getNetworkRegistration(int timeout_ms)
{
    return gsm_.getNetworkRegistration(timeout_ms);
}

std::string SystemMonitor::getModuleInfo(int timeout_ms)
{
    return gsm_.getModuleInfo(timeout_ms);
}

// 一次性获取所有GSM/4G模块信息（需要先调用 open_4g）
void SystemMonitor::getGsmInfo(std::string &signal, std::string &simStatus,
                                std::string &network, std::string &moduleInfo,
                                int timeout_ms)
{
    if (!gsm_initialized_) {
        signal = "N/A";
        simStatus = "N/A";
        network = "N/A";
        moduleInfo = "N/A";
        return;
    }
    
    signal = gsm_.getSignalQuality(timeout_ms);
    simStatus = gsm_.getSimStatus(timeout_ms);
    network = gsm_.getNetworkRegistration(timeout_ms);
    moduleInfo = gsm_.getModuleInfo(timeout_ms);
}