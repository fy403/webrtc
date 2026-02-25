#include "../include/async_monitor.h"
#include <iostream>

AsyncMonitor::AsyncMonitor()
    : running_(false), stopped_(true), interval_ms_(500)
{
}

AsyncMonitor::~AsyncMonitor()
{
    stop();
}

void AsyncMonitor::start()
{
    if (running_.load())
    {
        return;
    }

    stopped_.store(false);
    running_.store(true);
    collect_thread_ = std::thread(&AsyncMonitor::collectThread, this);
}

void AsyncMonitor::stop()
{
    if (!running_.load())
    {
        return;
    }

    stopped_.store(true);
    cv_.notify_one();
    if (collect_thread_.joinable())
    {
        collect_thread_.join();
    }
    running_.store(false);
}

void AsyncMonitor::collectThread()
{
    while (!stopped_.load())
    {
        try
        {
            // 调用子类实现的采集方法
            collectData();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Monitor collect error: " << e.what() << std::endl;
        }

        // 等待指定间隔
        std::unique_lock<std::mutex> lock(stats_mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(interval_ms_), [this] { return stopped_.load(); });
    }
}
