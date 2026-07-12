#include "audio_pipeline.h"

#include <iostream>
#include <chrono>
#include <cstring>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

AudioPipeline::AudioPipeline(LatencyTracker* tracker)
    : pa_out_(nullptr), tracker_(tracker),
      codec_ctx_(nullptr), swr_ctx_(nullptr),
      target_format_(AV_SAMPLE_FMT_S16) {}

AudioPipeline::~AudioPipeline() {
    cleanup();
}

bool AudioPipeline::init(PulseAudioOutput* pa_output) {
    pa_out_ = pa_output;
    
    // Find Opus decoder
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    if (!codec) {
        std::cerr << "[AudioPipeline] Opus decoder not found" << std::endl;
        return false;
    }
    
    // Allocate codec context
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "[AudioPipeline] Failed to allocate codec context" << std::endl;
        return false;
    }
    
    // Open decoder
    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << ("[AudioPipeline] Failed to open Opus decoder: " 
                      + std::string(errbuf)) << std::endl;
        avcodec_free_context(&codec_ctx_);
        return false;
    }
    
    std::cout << "[AudioPipeline] Initialized with Opus decoder" << std::endl;
    
    return true;
}

void AudioPipeline::cleanup() {
    stop();
    
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    
    input_queue_.close();
    pcm_buffer_.clear();
}

void AudioPipeline::push_rtp_data(const std::byte* data, size_t size) {
    AudioRTPPacket packet;
    packet.data.resize(size);
    memcpy(packet.data.data(), data, size);
    packet.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    input_queue_.push(std::move(packet));
    packets_received_.fetch_add(1);
    
    if (tracker_) tracker_->record_point(LatencyPoint::WEBRTC_RECEIVED);
}

bool AudioPipeline::start() {
    if (running_.load()) return true;
    
    running_.store(true);
    decode_thread_ = std::make_unique<std::thread>(&AudioPipeline::decode_loop, this);
    
    std::cout << "[AudioPipeline] Started decoding thread" << std::endl;
    return true;
}

void AudioPipeline::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    input_queue_.close();
    
    if (decode_thread_ && decode_thread_->joinable()) {
        decode_thread_->detach();
    }
    decode_thread_.reset();
    
    std::cout << "[AudioPipeline] Stopped" << std::endl;
}

void AudioPipeline::decode_loop() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* decoded_frame = av_frame_alloc();
    
    if (!pkt || !decoded_frame) {
        std::cerr << "[AudioPipeline] Failed to allocate packet/frame" << std::endl;
        if (pkt) av_packet_free(&pkt);
        if (decoded_frame) av_frame_free(&decoded_frame);
        return;
    }
    
    while (running_.load()) {
        AudioRTPPacket rtp_packet;
        
        if (!input_queue_.pop(rtp_packet)) {
            break;  // Queue closed
        }
        
        if (tracker_) tracker_->record_point(LatencyPoint::DECODE_STARTED);
        
        // Decode the Opus packet
        int ret = decode_packet(rtp_packet.data.data(), rtp_packet.data.size());
        
        if (ret < 0) {
            continue;  // Skip this packet
        }
        
        // Receive decoded frames and write to PulseAudio
        while (receive_frame(decoded_frame)) {
            if (tracker_) tracker_->record_point(LatencyPoint::DECODE_COMPLETED);
            
            // Convert to target format and write
            int samples_per_channel = decoded_frame->nb_samples;
            int total_samples = samples_per_channel * target_channels_;
            
            // Calculate buffer size for output format (16-bit samples)
            int out_size = av_samples_get_buffer_size(
                nullptr,
                target_channels_,
                samples_per_channel,
                target_format_,
                1  // alignment
            );
            
            if (out_size <= 0) continue;
            
            std::vector<uint8_t> pcm_data(out_size);
            
            // Setup resampler on first frame or format change
            if (!swr_ctx_ ||
                decoded_frame->format != codec_ctx_->sample_fmt ||
                decoded_frame->channels != codec_ctx_->channels ||
                decoded_frame->sample_rate != codec_ctx_->sample_rate)
            {
                if (swr_ctx_) {
                    swr_free(&swr_ctx_);
                    swr_ctx_ = nullptr;
                }
                
                // Initialize resampler
                swr_ctx_ = swr_alloc_set_opts(
                    nullptr,
                    AV_CH_LAYOUT_STEREO,     // Output layout
                    target_format_,           // Output format
                    target_sample_rate_,      // Output rate
                    av_get_default_channel_layout(decoded_frame->channels),  // Input layout
                    static_cast<AVSampleFormat>(decoded_frame->format),       // Input format
                    decoded_frame->sample_rate,  // Input rate
                    0, nullptr
                );
                
                if (swr_ctx_ && swr_init(swr_ctx_) < 0) {
                    std::cerr << "[AudioPipeline] Resampler init failed" << std::endl;
                    swr_free(&swr_ctx_);
                    swr_ctx_ = nullptr;
                    continue;
                }
            }
            
            // Perform resampling
            uint8_t* output_ptr = pcm_data.data();
            int converted_samples = swr_convert(
                swr_ctx_,
                &output_ptr,
                samples_per_channel,
                const_cast<const uint8_t**>(decoded_frame->data),
                samples_per_channel
            );
            
            if (converted_samples <= 0) continue;
            
            // Write to PulseAudio
            int actual_size = converted_samples * target_channels_ * sizeof(int16_t);
            if (actual_size > 0 && actual_size <= out_size) {
                if (write_pcm(pcm_data.data(), actual_size)) {
                    samples_played_.fetch_add(converted_samples * target_channels_);
                    if (tracker_) tracker_->record_point(LatencyPoint::PULSEAUDIO_WRITTEN);
                }
            }
        }
    }
    
    av_packet_free(&pkt);
    av_frame_free(&decoded_frame);
    
    std::cout <<("[AudioPipeline] Decode thread exited. Stats: packets=" +
              std::to_string(packets_received_.load()) +
              ", samples=" + std::to_string(samples_played_.load())) << std::endl;
}

int AudioPipeline::decode_packet(const uint8_t* data, size_t size) {
    AVPacket* pkt = av_packet_alloc();
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = static_cast<int>(size);
    
    int ret = avcodec_send_packet(codec_ctx_, pkt);
    av_packet_free(&pkt);
    
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        if (ret != AVERROR_INVALIDDATA) {
            std::cerr << "[AudioPipeline] send_packet error: " << errbuf << std::endl;
        }
    }
    
    return ret >= 0 ? 0 : ret;
}

// Note: receive_frame is inlined in decode_loop since we need frame access immediately after

bool AudioPipeline::receive_frame(AVFrame* frame) {
    if (!codec_ctx_) return false;
    int ret = avcodec_receive_frame(codec_ctx_, frame);
    return ret == 0;
}

bool AudioPipeline::write_pcm(const uint8_t* data, size_t size) {
    if (!pa_out_ || !pa_out_->is_open()) return false;
    
    return pa_out_->write(data, size);
}
