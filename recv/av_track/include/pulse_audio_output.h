#ifndef PULSE_AUDIO_OUTPUT_H
#define PULSE_AUDIO_OUTPUT_H

#include <string>
#include <atomic>
#include <cstdint>

// Opaque PulseAudio simple handle
typedef struct pa_simple pa_simple;

class PulseAudioOutput {
public:
    PulseAudioOutput();
    ~PulseAudioOutput();
    
    // Connect to PulseAudio virtual sink
    bool open(
        const std::string& sink_name,
        int sample_rate = 48000,
        int channels = 2);
    
    // Close connection
    void close();
    
    // Check if connected
    bool is_open() const { return pa_handle_ != nullptr; }
    
    // Write PCM data (interleaved 16-bit signed samples)
    bool write(const void* data, size_t bytes);
    
    // Flush buffered audio
    bool drain();
    
    // Get audio format info
    int get_sample_rate() const { return sample_rate_; }
    int get_channels() const { return channels_; }
    
    // Statistics
    uint64_t get_bytes_written() const { return bytes_written_.load(); }

private:
    std::string sink_name_;
    pa_simple* pa_handle_{nullptr};
    
    int sample_rate_{48000};
    int channels_{2};
    
    std::atomic<uint64_t> bytes_written_{0};
};

#endif // PULSE_AUDIO_OUTPUT_H
