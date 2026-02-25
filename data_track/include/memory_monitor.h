#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

#include <string>
#include <array>
#include "async_monitor.h"

class MemoryMonitor : public AsyncMonitor
{
public:
    MemoryMonitor();
    ~MemoryMonitor();

    // 获取内存信息
    bool getMemoryInfo(double &total_mb, double &used_mb, double &free_mb, double &usage_percent);

protected:
    // 实现基类的采集方法
    void collectData() override;

private:
    // 执行系统命令并获取输出
    std::string executeCommand(const std::string &command);

    // 内部采集方法（不使用锁，用于线程内部调用）
    bool getMemoryInfoInternal(double &total_mb, double &used_mb, double &free_mb, double &usage_percent);

    // 缓存的采集结果
    double cached_total_mb_;
    double cached_used_mb_;
    double cached_free_mb_;
    double cached_usage_percent_;
};

#endif // MEMORY_MONITOR_H
