#ifndef ASYNC_MONITOR_H
#define ASYNC_MONITOR_H

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>

class AsyncMonitor
{
public:
    AsyncMonitor();
    virtual ~AsyncMonitor();

    // 启动监控线程
    void start();

    // 停止监控线程
    void stop();

    // 获取采集间隔（毫秒）
    int getInterval() const { return interval_ms_; }
    void setInterval(int interval_ms) { interval_ms_ = interval_ms; }

protected:
    // 子类需要实现采集方法
    virtual void collectData() = 0;

    // 采集线程函数
    void collectThread();

    // 异步采集相关
    std::thread collect_thread_;
    std::mutex stats_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
    std::atomic<bool> stopped_;

    // 采集间隔（毫秒）
    int interval_ms_;
};

#endif // ASYNC_MONITOR_H
