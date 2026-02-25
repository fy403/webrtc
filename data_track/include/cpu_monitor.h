#ifndef CPU_MONITOR_H
#define CPU_MONITOR_H

#include <string>
#include <array>
#include <vector>
#include "async_monitor.h"

class CPUMonitor : public AsyncMonitor
{
public:
    CPUMonitor();
    ~CPUMonitor();

    // 获取CPU使用率
    bool getUsage(double &cpu_usage);

    // 获取详细的CPU信息
    struct CPUInfo
    {
        double total_usage;       // 总CPU使用率(%)
        int core_count;           // CPU核心数
        std::vector<double> core_usage; // 每个核心使用率(%)
        int total_tasks;          // 总任务数
        int running_tasks;        // 运行中的任务数
        int sleeping_tasks;       // 睡眠中的任务数
        double load_1min;         // 1分钟平均负载
        double load_5min;         // 5分钟平均负载
        double load_15min;        // 15分钟平均负载
    };

    bool getCPUInfo(CPUInfo &info);

protected:
    // 实现基类的采集方法
    void collectData() override;

private:
    // 执行系统命令并获取输出
    std::string executeCommand(const std::string &command);

    // 内部采集方法（不使用锁，用于线程内部调用）
    bool getUsageInternal(double &cpu_usage);
    bool getCPUInfoInternal(CPUInfo &info);

    // CPU使用率统计
    unsigned long long last_total_jiffies_;
    unsigned long long last_work_jiffies_;
    std::vector<unsigned long long> last_core_total_jiffies_;
    std::vector<unsigned long long> last_core_work_jiffies_;

    // 缓存的采集结果
    double cached_cpu_usage_;
    CPUInfo cached_cpu_info_;
};

#endif // CPU_MONITOR_H
