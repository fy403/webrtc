#ifndef LATENCY_TRACKER_H
#define LATENCY_TRACKER_H

#include <chrono>
#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <iostream>

/**
 * 端到端延时跟踪器
 * 
 * 在视频处理流水线的每个阶段记录时间戳，计算各阶段耗时：
 *   [Capture] → [Decode] → [Filter] → [Encode] → [Send] → [WebRTC] → [Network] → [Render]
 * 
 * 使用方式：
 *   1. capture 阶段：record_capture(frame_id)
 *   2. decode 阶段：record_decode(frame_id)
 *   3. filter 阶段：record_filter(frame_id)
 *   4. encode 阶段：record_encode(frame_id)
 *   5. send 阶段（WebRTC sendFrame）：record_send(frame_id)
 *   6. 前端通过 DataChannel 回传 render 时间戳
 */
class LatencyTracker {
public:
    // 流水线阶段枚举
    enum Stage {
        CAPTURE = 0,    // 摄像头采集完成
        DECODE = 1,     // 解码完成
        FILTER = 2,     // FPS滤镜完成
        ENCODE = 3,     // 编码完成
        SEND = 4,       // WebRTC track.sendFrame 调用
        NETWORK = 5,    // 网络传输（RTT，由前端回传）
        RENDER = 6,     // 浏览器渲染（由前端回传）
        STAGE_COUNT     // 阶段总数
    };

    static constexpr const char* STAGE_NAMES[] = {
        "Capture", "Decode", "Filter", "Encode", "Send", "Network", "Render"
    };

    // 单帧的时间戳记录
    struct FrameTimestamps {
        uint64_t frame_id;
        std::chrono::system_clock::time_point times[STAGE_COUNT];
        bool has_stage[STAGE_COUNT];

        FrameTimestamps() : frame_id(0) {
            for (int i = 0; i < STAGE_COUNT; ++i) {
                has_stage[i] = false;
            }
        }
    };

    // 延时统计摘要
    struct LatencyStats {
        // 各阶段的平均延时 (微秒)
        double avg_stage_us[STAGE_COUNT];
        // 各阶段的最大延时 (微秒)
        double max_stage_us[STAGE_COUNT];
        // 端到端总平均延时 (毫秒)
        double total_avg_ms;
        // 端到端最大延时 (毫秒)
        double total_max_ms;
        // 已统计帧数
        uint64_t frame_count;

        LatencyStats() {
            for (int i = 0; i < STAGE_COUNT; ++i) {
                avg_stage_us[i] = 0;
                max_stage_us[i] = 0;
            }
            total_avg_ms = 0;
            total_max_ms = 0;
            frame_count = 0;
        }
    };

    explicit LatencyTracker(size_t ring_size = 256);

    // ====== 各阶段打点 ======

    // 记录采集时间戳
    void record_capture(uint64_t frame_id);
    
    // 记录解码完成时间戳
    void record_decode(uint64_t frame_id);
    
    // 记录滤镜输出时间戳
    void record_filter(uint64_t frame_id);
    
    // 记录编码完成时间戳
    void record_encode(uint64_t frame_id);
    
    // 记录 WebRTC 发送时间戳
    void record_send(uint64_t frame_id);
    
    // 记录前端回传的网络+渲染时间戳（用于计算 E2E）
    void record_network_render(uint64_t frame_id, double network_ms, double render_ms);

    // ====== 统计查询 ======

    // 获取当前累计统计
    LatencyStats get_stats() const;
    
    // 重置统计
    void reset();
    
    // 打印当前统计到 stdout（带 ANSI 颜色）
    void print_stats() const;

    // 打印单帧各阶段延时（调试用）
    void print_frame(uint64_t frame_id) const;

    // 获取最新帧 ID
    uint64_t get_last_frame_id() const { return last_frame_id_.load(); }

    // 导出最近 N 帧的逐帧阶段延时数据（供 DataChannel 发送到前端）
    // 返回 JSON 字符串
    std::string dump_recent_frames_json(size_t max_count = 30) const;

    // 获取 send 时间戳映射（frame_id → send_time_us）
    // 用于前端计算每帧网络延时
    std::vector<std::pair<uint64_t, uint64_t>> get_recent_send_times(size_t max_count = 60) const;

private:
    // 环形缓冲区索引
    size_t index_of(uint64_t frame_id) const { return frame_id % ring_size_; }

    size_t ring_size_;
    std::unique_ptr<FrameTimestamps[]> frames_;
    
    std::atomic<uint64_t> last_frame_id_{0};
    mutable std::mutex stats_mutex_;
};

#endif // LATENCY_TRACKER_H
