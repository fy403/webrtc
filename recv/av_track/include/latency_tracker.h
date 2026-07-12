#ifndef LATENCY_TRACKER_H
#define LATENCY_TRACKER_H

#include <chrono>
#include <atomic>
#include <string>
#include <map>
#include <mutex>
#include <iostream>

// Latency measurement points in the pipeline
enum class LatencyPoint {
    WEBRTC_RECEIVED,      // Data received via WebRTC onTrack
    DECODE_STARTED,       // Decoding started
    DECODE_COMPLETED,     // Frame/packet fully decoded
    V4L2_WRITTEN,         // Frame written to V4L2 device
    PULSEAUDIO_WRITTEN    // PCM written to PulseAudio
};

class LatencyTracker {
public:
    LatencyTracker();
    
    // Record timestamp at a specific point
    void record_point(LatencyPoint point);
    
    // Calculate elapsed time between two points (in milliseconds)
    double calculate_latency_ms(LatencyPoint start, LatencyPoint end) const;
    
    // Print real-time statistics to console
    void print_stats(double interval_seconds = 1.0);
    
    // Enable/disable tracking
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_.load(); }
    
    // Reset all measurements
    void reset();
    
    // Public counters for external update
    std::atomic<uint64_t> video_frames_{0};
    std::atomic<uint64_t> audio_packets_{0};

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    
    std::atomic<bool> enabled_{true};
    
    struct PointData {
        std::atomic<uint64_t> last_timestamp_ns{0};
        std::atomic<double> avg_latency_ms{0};
        std::atomic<double> max_latency_ms{0};
        std::atomic<double> min_latency_ms{99999};
        std::atomic<uint64_t> count{0};
    };
    
    std::map<LatencyPoint, PointData> points_;
    mutable std::mutex mtx_;
    
    std::atomic<double> video_bitrate_kbps_{0};
    std::atomic<int> video_fps_{0};
    
    // Helper to get current time as nanoseconds
    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count();
    }
    
    static const char* point_name(LatencyPoint p);
};

#endif // LATENCY_TRACKER_H
