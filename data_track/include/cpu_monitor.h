#ifndef CPU_MONITOR_H
#define CPU_MONITOR_H

#include <string>
#include <array>

class CPUMonitor {
public:
    CPUMonitor();
    ~CPUMonitor();

    // 获取CPU使用率
    bool getUsage(double &cpu_usage);

private:
    // 执行系统命令并获取输出
    std::string executeCommand(const std::string &command);

    // CPU使用率统计
    unsigned long long last_total_jiffies_;
    unsigned long long last_work_jiffies_;
};

#endif // CPU_MONITOR_H
