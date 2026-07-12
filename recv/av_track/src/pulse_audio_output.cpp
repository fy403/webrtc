#include "pulse_audio_output.h"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <iostream>
#include <cstring>

PulseAudioOutput::PulseAudioOutput() {}

PulseAudioOutput::~PulseAudioOutput() {
    close();
}

bool PulseAudioOutput::open(const std::string& sink_name, int sample_rate, int channels) {
    if (pa_handle_) {
        close();  // Close existing connection
    }
    
    sink_name_ = sink_name;
    sample_rate_ = sample_rate;
    channels_ = channels;
    
    // Configure PulseAudio stream specification
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;  // Signed 16-bit Little Endian
    ss.rate = sample_rate_;
    ss.channels = static_cast<uint8_t>(channels);
    
    if (!pa_sample_spec_valid(&ss)) {
        std::cerr << "[PulseAudio] Invalid sample specification" << std::endl;
        return false;
    }
    
    // Set up channel map (stereo or mono)
    pa_channel_map cmap;
    if (channels == 1) {
        pa_channel_map_init_mono(&cmap);
    } else if (channels == 2) {
        pa_channel_map_init_stereo(&cmap);
    } else {
        // Default channel map for other channel counts
        pa_channel_map_init_auto(&cmap, channels, PA_CHANNEL_MAP_DEFAULT);
    }
    
    // Create PulseAudio simple connection
    // Try specified sink first; fall back to default device if sink doesn't exist
    int error;
    
    // Try with specified sink name first
    const char* sink = sink_name.empty() ? nullptr : sink_name.c_str();
    if (sink) {
        pa_handle_ = pa_simple_new(
            nullptr, "WebRTC Receiver",
            PA_STREAM_PLAYBACK, sink,
            "WebRTC Audio Stream",
            &ss, &cmap, nullptr, &error);
        if (!pa_handle_) {
            std::cout << "[PulseAudio] Sink '" << sink_name_ 
                      << "' not available (" << pa_strerror(error)
                      << "), trying default device..." << std::endl;
        }
    }
    
    // Fall back to default sink
    if (!pa_handle_) {
        pa_handle_ = pa_simple_new(
            nullptr, "WebRTC Receiver",
            PA_STREAM_PLAYBACK, nullptr,           // default sink
            "WebRTC Audio Stream",
            &ss, &cmap, nullptr, &error);
    }
    
    if (!pa_handle_) {
        std::cerr << "[PulseAudio] Failed to connect: " 
                  << pa_strerror(error) << std::endl;
        return false;
    }
    
    bytes_written_.store(0);
    
    std::cout <<("[PulseAudio] Connected: " + sink_name +
              ", " + std::to_string(sample_rate_) + "Hz" +
              ", " + std::to_string(channels_) + "ch") << std::endl;
    
    return true;
}

void PulseAudioOutput::close() {
    if (pa_handle_) {
        // Drain remaining audio before closing
        drain();
        
        pa_simple_free(pa_handle_);
        pa_handle_ = nullptr;
        
        std::cout << "[PulseAudio] Disconnected from: " << sink_name_ << std::endl;
    }
}

bool PulseAudioOutput::write(const void* data, size_t bytes) {
    if (!pa_handle_ || !data || bytes == 0) return false;
    
    int error;
    
    // Write PCM data to PulseAudio (blocking call)
    if (pa_simple_write(pa_handle_, data, bytes, &error) < 0) {
        std::cerr << "[PulseAudio] Write failed: " 
                  << pa_strerror(error) << std::endl;
        return false;
    }
    
    bytes_written_.fetch_add(bytes);
    return true;
}

bool PulseAudioOutput::drain() {
    if (!pa_handle_) return false;
    
    int error;
    
    // Wait for all buffered audio to play
    if (pa_simple_drain(pa_handle_, &error) < 0) {
        std::cerr << "[PulseAudio] Drain failed: " 
                  << pa_strerror(error) << std::endl;
        return false;
    }
    
    return true;
}
