#include "../include/service_monitor.h"
#include <algorithm>
#include <iostream>
#include <memory>

ServiceMonitor::ServiceMonitor() {}
ServiceMonitor::~ServiceMonitor() {}

bool ServiceMonitor::checkServiceStatus(const std::string &service_name)
{
    try
    {
        std::string command = "systemctl is-active " + service_name + " 2>/dev/null";
        std::string result = executeCommand(command);

        // 移除换行符并检查结果
        result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
        return (result == "active");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to check service status: " << service_name << " - " << e.what() << std::endl;
        return false;
    }
}

void ServiceMonitor::checkMultipleServices(const std::vector<std::string> &service_names,
                                           std::map<std::string, bool> &service_status)
{
    service_status.clear();
    for (const auto &service_name : service_names)
    {
        service_status[service_name] = checkServiceStatus(service_name);
    }
}

std::string ServiceMonitor::executeCommand(const std::string &command)
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
