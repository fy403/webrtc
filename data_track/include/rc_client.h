#ifndef RC_CLIENT_H
#define RC_CLIENT_H

#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <set>
#include <nlohmann/json.hpp>
#include "motor_controller.h"
#include "system_monitor.h"
#include "constants.h"
#include "rtc/rtc.hpp"
#include "rc_client_config.h"

class RCClient {
public:
    /**
     * 构造函数 - 使用配置类
     * @param config RCClient 系统配置，包含所有子组件的配置参数
     */
    explicit RCClient(const RCClientConfig &config = RCClientConfig());

    // 禁止拷贝构造和赋值
    RCClient(const RCClient &) = delete;

    RCClient &operator=(const RCClient &) = delete;

    ~RCClient();

    void stopAll();

    void parseFrame(const std::string &peer_id, const uint8_t *frame, size_t length);

    void sendSystemStatus();

    /**
     * 添加DataChannel（支持多端连接）
     * @param peer_id 对端ID
     * @param dc DataChannel智能指针
     */
    void addDataChannel(const std::string &peer_id, std::shared_ptr<rtc::DataChannel> dc);

    /**
     * 移除DataChannel
     * @param peer_id 对端ID
     */
    void removeDataChannel(const std::string &peer_id);

    /**
     * 获取当前连接的DataChannel数量
     */
    size_t getDataChannelCount() const;

private:
    MotorController *motor_controller_;
    std::atomic<bool> has_timeout_;

    // parseFrame互斥锁（多端并发控制保护）
    std::mutex parse_frame_mutex_;

    // 跟踪最后发送控制命令的peer_id
    std::string last_control_peer_id_;
    mutable std::mutex last_control_peer_id_mutex_;

    // Data channels for WebRTC communication（支持多端连接）
    std::unordered_map<std::string, std::shared_ptr<rtc::DataChannel> > data_channels_;
    mutable std::mutex data_channels_mutex_;

    // DataChannel健康检查
    struct DataChannelHealth {
        std::chrono::steady_clock::time_point last_heartbeat;
        bool is_alive;
        std::atomic<int> missed_heartbeat_count;

        // 每个DC独立的watchdog参数
        int heartbeat_count;
        int64_t total_heartbeat_interval_ms;
        std::chrono::steady_clock::time_point last_heartbeat_time;
    };

    std::unordered_map<std::string, DataChannelHealth> channel_health_;
    mutable std::mutex channel_health_mutex_;

    // DataChannel健康检查线程
    std::thread health_check_thread_;
    std::atomic<bool> health_check_running_;
    const int HEALTH_CHECK_INTERVAL_MS = 500;
    const int MAX_MISSED_HEARTBEATS = 2;
    int default_watchdog_timeout_ms_;

    // System monitor
    SystemMonitor system_monitor_;
    std::chrono::steady_clock::time_point last_status_time_;

    void sendStatusFrame(const std::map<std::string, std::string> &statusData);

    void healthCheckLoop();
};

void signalHandler(int signal);

#endif // RC_CLIENT_H
