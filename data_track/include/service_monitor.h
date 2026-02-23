#ifndef SERVICE_MONITOR_H
#define SERVICE_MONITOR_H

#include <string>
#include <map>
#include <vector>
#include <array>

class ServiceMonitor
{
public:
    ServiceMonitor();
    ~ServiceMonitor();

    // 检查单个服务状态
    bool checkServiceStatus(const std::string &service_name);

    // 一次性获取多个服务状态
    void checkMultipleServices(const std::vector<std::string> &service_names,
                               std::map<std::string, bool> &service_status);

private:
    // 执行系统命令并获取输出
    std::string executeCommand(const std::string &command);
};

#endif // SERVICE_MONITOR_H
