#ifndef VIDEO_PIPELINE_H
#define VIDEO_PIPELINE_H

#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "rtc/rtc.hpp"
#include "v4l2_output.h"
#include "frame_buffer.h"
#include "latency_tracker.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

// RTP packet container for decoded processing
struct VideoRTPPacket {
    std::vector<uint8_t> data;
    int64_t timestamp_us;  // microseconds since epoch or relative
};

class VideoPipeline {
public:
    VideoPipeline(LatencyTracker* tracker = nullptr);
    ~VideoPipeline();
    
    // Initialize decoder and V4L2 output
    bool init(V4LOutput* v4l2_output, const std::string& codec_hint = "h264");
    
    // Cleanup resources
    void cleanup();
    
    // Push raw RTP payload data (called from WebRTC onTrack callback)
    void push_rtp_data(const std::byte* data, size_t size);
    
    // Start the decoding thread
    bool start();
    
    // Stop decoding thread
    void stop();
    
    // Check if running
    bool is_running() const { return running_.load(); }
    
    // Get statistics
    int get_width() const;
    int get_height() const;
    double get_fps() const;
    uint64_t get_frames_decoded() const;
    uint64_t get_frames_dropped() const;

private:
    V4LOutput* v4l2_out_;
    LatencyTracker* tracker_;
    
    // Codec context
    AVCodecContext* codec_ctx_{nullptr};
    AVCodecParserContext* parser_ctx_{nullptr};
    SwsContext* sws_ctx_{nullptr};
    
    // Codec type (AV_CODEC_ID_H264 or H265)
    enum AVCodecID codec_id_;
    
    // Thread control
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> decode_thread_;
    
    // Input queue (RTP packets from WebRTC)
    SafeQueue<VideoRTPPacket> input_queue_;
    
    // Output buffer (decoded frames waiting for V4L2 write)
    SafeQueue<AVFrame*> output_queue_;
    
    // Statistics
    std::atomic<uint64_t> frames_decoded_{0};
    std::atomic<uint64_t> frames_dropped_{0};
    std::atomic<int64_t> last_timestamp_{0};
    
    // Decode loop (runs in separate thread)
    void decode_loop();
    
    // Feed data to FFmpeg parser/decoder
    int feed_decoder(const uint8_t* data, size_t size);
    
    // Receive decoded frame from decoder
    bool receive_frame(AVFrame* frame);
    
    // Convert and write frame to V4L2
    bool output_frame(AVFrame* frame);
    
    // Detect codec from SDP or first packet
    bool detect_codec(const std::string& hint);
};

#endif // VIDEO_PIPELINE_H
