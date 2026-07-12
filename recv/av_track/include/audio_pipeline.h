#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "pulse_audio_output.h"
#include "frame_buffer.h"
#include "latency_tracker.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

// RTP packet container for audio
struct AudioRTPPacket {
    std::vector<uint8_t> data;
    int64_t timestamp_us;
};

class AudioPipeline {
public:
    AudioPipeline(LatencyTracker* tracker = nullptr);
    ~AudioPipeline();
    
    // Initialize Opus decoder and PA output
    bool init(PulseAudioOutput* pa_output);
    
    // Cleanup resources
    void cleanup();
    
    // Push raw RTP payload data (Opus encoded)
    void push_rtp_data(const std::byte* data, size_t size);
    
    // Start decoding thread
    bool start();
    
    // Stop decoding thread
    void stop();
    
    // Check if running
    bool is_running() const { return running_.load(); }
    
    // Get statistics
    uint64_t get_samples_played() const { return samples_played_.load(); }
    uint64_t get_packets_received() const { return packets_received_.load(); }

private:
    PulseAudioOutput* pa_out_;
    LatencyTracker* tracker_;
    
    // Decoder context
    AVCodecContext* codec_ctx_{nullptr};
    SwrContext* swr_ctx_{nullptr};
    
    // Resampling parameters
    int target_sample_rate_{48000};
    int target_channels_{2};
    enum AVSampleFormat target_format_{AV_SAMPLE_FMT_S16};
    
    // Thread control
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> decode_thread_;
    
    // Input queue (RTP packets from WebRTC)
    SafeQueue<AudioRTPPacket> input_queue_;
    
    // Output buffer (PCM samples ready for PA)
    SafeQueue<std::vector<uint8_t>> pcm_buffer_;
    
    // Statistics
    std::atomic<uint64_t> samples_played_{0};
    std::atomic<uint64_t> packets_received_{0};
    
    // Decode loop
    void decode_loop();
    
    // Decode one packet
    int decode_packet(const uint8_t* data, size_t size);
    
    // Receive decoded frame from codec
    bool receive_frame(AVFrame* frame);
    
    // Write PCM data to PulseAudio
    bool write_pcm(const uint8_t* data, size_t size);
};

#endif // AUDIO_PIPELINE_H
