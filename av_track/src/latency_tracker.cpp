#include "latency_tracker.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <inttypes.h>

LatencyTracker::LatencyTracker(size_t ring_size) 
    : ring_size_(ring_size > 0 ? ring_size : 256),
      frames_(std::make_unique<FrameTimestamps[]>(ring_size_)) {}

void LatencyTracker::record_capture(uint64_t frame_id) {
    size_t idx = index_of(frame_id);
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto& frame = frames_[idx];
    // 重置该槽位（新帧开始，清除旧数据）
    frame = FrameTimestamps{};
    frame.frame_id = frame_id;
    frame.times[Stage::CAPTURE] = std::chrono::steady_clock::now();
    frame.has_stage[Stage::CAPTURE] = true;
    last_frame_id_.store(frame_id);
}

void LatencyTracker::record_decode(uint64_t frame_id) {
    size_t idx = index_of(frame_id);
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto& frame = frames_[idx];
    if (frame.frame_id == frame_id || !frame.has_stage[Stage::DECODE]) {
        frame.times[Stage::DECODE] = std::chrono::steady_clock::now();
        frame.has_stage[Stage::DECODE] = true;
    }
}

void LatencyTracker::record_filter(uint64_t frame_id) {
    size_t idx = index_of(frame_id);
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto& frame = frames_[idx];
    if (frame.frame_id == frame_id || !frame.has_stage[Stage::FILTER]) {
        frame.times[Stage::FILTER] = std::chrono::steady_clock::now();
        frame.has_stage[Stage::FILTER] = true;
    }
}

void LatencyTracker::record_encode(uint64_t frame_id) {
    size_t idx = index_of(frame_id);
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto& frame = frames_[idx];
    if (frame.frame_id == frame_id || !frame.has_stage[Stage::ENCODE]) {
        frame.times[Stage::ENCODE] = std::chrono::steady_clock::now();
        frame.has_stage[Stage::ENCODE] = true;
    }
}

void LatencyTracker::record_send(uint64_t frame_id) {
    size_t idx = index_of(frame_id);
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto& frame = frames_[idx];
    if (frame.frame_id == frame_id || !frame.has_stage[Stage::SEND]) {
        frame.times[Stage::SEND] = std::chrono::steady_clock::now();
        frame.has_stage[Stage::SEND] = true;
    }
}

void LatencyTracker::record_network_render(uint64_t frame_id, double network_ms, double render_ms) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    size_t idx = index_of(frame_id);
    auto& frame = frames_[idx];
    if (frame.frame_id != frame_id) return;
    // 标记该帧有完整的 E2E 数据
    (void)network_ms;
    (void)render_ms;
}

LatencyTracker::LatencyStats LatencyTracker::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    LatencyStats stats;
    uint64_t count = 0;
    double total_us = 0;
    double total_max_us = 0;
    
    // 各阶段累计
    double stage_sum_us[STAGE_COUNT] = {0};
    double stage_max_us[STAGE_COUNT] = {0};
    
    uint64_t last_id = last_frame_id_.load();
    // 只统计最近 min(ring_size_, last_frame_id) 帧
    size_t sample_count = std::min(ring_size_, static_cast<size_t>(last_id + 1));
    uint64_t start_id = (last_id >= sample_count) ? (last_id - sample_count + 1) : 0;

    for (size_t i = 0; i < sample_count; ++i) {
        uint64_t fid = start_id + i;
        const auto& frame = frames_[index_of(fid)];
        
        // 跳过不匹配的帧（环形缓冲区覆盖了旧数据）
        if (frame.frame_id != fid) continue;
        
        // 计算各阶段间延时（微秒）
        double prev_time_us = 0;
        bool has_prev = false;
        double frame_total_us = 0;
        
        for (int s = 0; s <= Stage::SEND; ++s) {
            if (!frame.has_stage[s]) continue;
            
            auto time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                frame.times[s].time_since_epoch()).count();
            
            if (has_prev) {
                double delta_us = time_us - prev_time_us;
                stage_sum_us[s] += delta_us;
                if (delta_us > stage_max_us[s]) {
                    stage_max_us[s] = delta_us;
                }
                frame_total_us += delta_us;
            }
            
            prev_time_us = time_us;
            has_prev = true;
        }

        // 端到端：capture → send
        if (frame.has_stage[Stage::CAPTURE] && frame.has_stage[Stage::SEND]) {
            auto capture_us = std::chrono::duration_cast<std::chrono::microseconds>(
                frame.times[Stage::CAPTURE].time_since_epoch()).count();
            auto send_us = std::chrono::duration_cast<std::chrono::microseconds>(
                frame.times[Stage::SEND].time_since_epoch()).count();
            double e2e_us = send_us - capture_us;
            total_us += e2e_us;
            if (e2e_us > total_max_us) {
                total_max_us = e2e_us;
            }
            ++count;
        }
    }

    stats.frame_count = count;
    if (count > 0) {
        for (int s = 1; s <= Stage::SEND; ++s) {  // 从 DECODE 开始（CAPTURE 是起点）
            stats.avg_stage_us[s] = stage_sum_us[s] / count;
            stats.max_stage_us[s] = stage_max_us[s];
        }
        stats.total_avg_ms = total_us / count / 1000.0;       // 转毫秒
        stats.total_max_ms = total_max_us / 1000.0;
    }

    return stats;
}

void LatencyTracker::reset() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    for (size_t i = 0; i < ring_size_; ++i) {
        frames_[i] = FrameTimestamps{};
    }
    last_frame_id_.store(0);
}

void LatencyTracker::print_stats() const {
    auto stats = get_stats();

    if (stats.frame_count == 0) {
        std::cout << "[Latency] No data yet" << std::endl;
        return;
    }

    // ANSI 颜色代码
    const char* GREEN = "\033[32m";
    const char* YELLOW = "\033[33m";
    const char* RED = "\033[31m";
    const char* CYAN = "\033[36m";
    const char* BOLD = "\033[1m";
    const char* RESET = "\033[0m";

    auto color_for_ms = [&](double ms) -> const char* {
        if (ms < 5) return GREEN;
        if (ms < 15) return YELLOW;
        return RED;
    };

    std::cout << BOLD << "╔════════════════════════════════════════╗" << RESET << std::endl;
    std::cout << BOLD << "║" << CYAN << "     End-to-End Latency Report         " << RESET << BOLD << "║" << RESET << std::endl;
    std::cout << BOLD << "╠════════════════════════════════════════╣" << RESET << std::endl;
    
    // 各阶段延时
    const char* stage_labels[] = {"", "Decode", "Filter", "Encode", "Send"};
    for (int s = Stage::DECODE; s <= Stage::SEND; ++s) {
        double avg_ms = stats.avg_stage_us[s] / 1000.0;
        double max_ms = stats.max_stage_us[s] / 1000.0;
        printf("║  %-8s %s%6.1fms%s avg  %s%6.1fms%s max  ║\n",
               stage_labels[s],
               color_for_ms(avg_ms), avg_ms, RESET,
               color_for_ms(max_ms), max_ms, RESET);
    }

    std::cout << BOLD << "╠──────────────────────────────────────────╣" << RESET << std::endl;
    
    // 端到端总延时
    double e2e_avg = stats.total_avg_ms;
    double e2e_max = stats.total_max_ms;
    const char* e2e_color = (e2e_avg < 50) ? GREEN : (e2e_avg < 100) ? YELLOW : RED;
    
    printf("║  %-8s %s%6.1fms%s avg  %s%6.1fms%s max  ║\n",
           "E2E Total",
           e2e_color, e2e_avg, RESET,
           e2e_color, e2e_max, RESET);

    printf("║  Frames: %-38" PRIu64 "║\n", stats.frame_count);
    std::cout << BOLD << "╚════════════════════════════════════════╝" << RESET << std::endl;
}

void LatencyTracker::print_frame(uint64_t frame_id) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    const auto& frame = frames_[index_of(frame_id)];
    if (frame.frame_id != frame_id) {
        std::cout << "[Latency] Frame " << frame_id << " not found in buffer" << std::endl;
        return;
    }

    std::cout << "[Latency] Frame " << frame_id << " timeline:" << std::endl;
    auto prev_time = std::chrono::steady_clock::time_point{};
    bool has_prev = false;

    for (int s = 0; s <= Stage::SEND; ++s) {
        if (!frame.has_stage[s]) continue;
        
        auto now = frame.times[s];
        if (has_prev) {
            auto delta_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now - prev_time).count();
            std::cout << "  → " << STAGE_NAMES[s] << ": +" 
                      << (delta_us / 1000.0) << "ms (" << delta_us << "us)" << std::endl;
        } else {
            std::cout << "  ● " << STAGE_NAMES[s] << ": (start)" << std::endl;
        }
        prev_time = now;
        has_prev = true;
    }
}
