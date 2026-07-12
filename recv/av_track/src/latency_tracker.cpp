#include "latency_tracker.h"
#include <iomanip>
#include <sstream>

LatencyTracker::LatencyTracker() {
    // Initialize all points (atomic members prevent copy, use try_emplace)
    for (int i = static_cast<int>(LatencyPoint::WEBRTC_RECEIVED); 
         i <= static_cast<int>(LatencyPoint::PULSEAUDIO_WRITTEN); 
         i++) {
        points_.try_emplace(static_cast<LatencyPoint>(i));
    }
}

void LatencyTracker::record_point(LatencyPoint point) {
    if (!enabled_.load()) return;
    
    auto now = now_ns();
    
    if (auto it = points_.find(point); it != points_.end()) {
        it->second.last_timestamp_ns.store(now);
    }
}

double LatencyTracker::calculate_latency_ms(LatencyPoint start, LatencyPoint end) const {
    auto start_it = points_.find(start);
    auto end_it = points_.find(end);
    
    if (start_it == points_.end() || end_it == points_.end()) {
        return 0.0;
    }
    
    uint64_t start_ns = start_it->second.last_timestamp_ns.load();
    uint64_t end_ns = end_it->second.last_timestamp_ns.load();
    
    if (start_ns == 0 || end_ns == 0 || end_ns < start_ns) {
        return 0.0;
    }
    
    return static_cast<double>(end_ns - start_ns) / 1e6;  // Convert to milliseconds
}

void LatencyTracker::print_stats(double interval_seconds) {
    if (!enabled_.load()) return;
    
    // Calculate latencies for video pipeline
    double decode_latency = calculate_latency_ms(
        LatencyPoint::DECODE_STARTED,
        LatencyPoint::DECODE_COMPLETED
    );
    
    double total_video_latency = calculate_latency_ms(
        LatencyPoint::WEBRTC_RECEIVED,
        LatencyPoint::V4L2_WRITTEN
    );
    
    // Calculate latencies for audio pipeline  
    double total_audio_latency = calculate_latency_ms(
        LatencyPoint::WEBRTC_RECEIVED,
        LatencyPoint::PULSEAUDIO_WRITTEN
    );
    
    std::cout << "\n═══════════════════════════════════════" << std::endl;
    std::cout << "  📊 LATENCY STATISTICS" << std::endl;
    std::cout << "═══════════════════════════════════════" << std::endl;
    
    std::cout << "  Video Pipeline:" << std::endl;
    std::cout << "    Decode:   " << std::fixed << std::setprecision(1) 
              << decode_latency << " ms" << std::endl;
    std::cout << "    E2E Total:" << total_video_latency << " ms" << std::endl;
    
    std::cout << "  Audio Pipeline:" << std::endl;
    std::cout << "    E2E Total:" << total_audio_latency << " ms" << std::endl;
    
    std::cout << "  Counters:" << std::endl;
    std::cout << "    Video frames: " << video_frames_.load() 
              << " | Audio packets: " << audio_packets_.load()
              << " | Bitrate: " << std::setprecision(0)
              << video_bitrate_kbps_.load() << " Kbps"
              << " | FPS: " << video_fps_.load() << std::endl;
    
    std::cout << "═══════════════════════════════════════" << std::endl << std::endl;
}

void LatencyTracker::reset() {
    for (auto& [point, data] : points_) {
        data.last_timestamp_ns.store(0);
        data.avg_latency_ms.store(0);
        data.max_latency_ms.store(0);
        data.min_latency_ms.store(99999);
        data.count.store(0);
    }
    
    video_frames_.store(0);
    audio_packets_.store(0);
    video_bitrate_kbps_.store(0);
    video_fps_.store(0);
}

const char* LatencyTracker::point_name(LatencyPoint p) {
    switch (p) {
        case LatencyPoint::WEBRTC_RECEIVED:
            return "WebRTC_Received";
        case LatencyPoint::DECODE_STARTED:
            return "Decode_Started";
        case LatencyPoint::DECODE_COMPLETED:
            return "Decode_Completed";
        case LatencyPoint::V4L2_WRITTEN:
            return "V4L2_Written";
        case LatencyPoint::PULSEAUDIO_WRITTEN:
            return "PulseAudio_Written";
        default:
            return "Unknown";
    }
}
