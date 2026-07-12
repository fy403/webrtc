#include "video_pipeline.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <linux/videodev2.h>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

VideoPipeline::VideoPipeline(LatencyTracker* tracker)
    : v4l2_out_(nullptr), tracker_(tracker),
      codec_ctx_(nullptr), parser_ctx_(nullptr), sws_ctx_(nullptr),
      codec_id_(AV_CODEC_ID_H264) {}

VideoPipeline::~VideoPipeline() {
    cleanup();
}

bool VideoPipeline::init(V4LOutput* v4l2_output, const std::string& codec_hint) {
    v4l2_out_ = v4l2_output;
    
    // Detect codec from hint or default to H.264
    if (!detect_codec(codec_hint)) {
        std::cerr << "[VideoPipeline] Failed to detect/initialize codec" << std::endl;
        return false;
    }
    
    return true;
}

bool VideoPipeline::detect_codec(const std::string& hint) {
    std::string lower_hint = hint;
    std::transform(lower_hint.begin(), lower_hint.end(),
                  lower_hint.begin(), ::tolower);
    
    if (lower_hint.find("h265") != std::string::npos || 
        lower_hint.find("hevc") != std::string::npos)
    {
        codec_id_ = AV_CODEC_ID_H265;
    } else {
        codec_id_ = AV_CODEC_ID_H264;  // Default
    }
    
    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(codec_id_);
    if (!codec) {
        std::cerr << "[VideoPipeline] Codec not found: " << hint << std::endl;
        return false;
    }
    
    // Allocate codec context
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "[VideoPipeline] Failed to allocate codec context" << std::endl;
        return false;
    }
    
    // Enable low-latency options (for H.264/H.265)
    if (codec_id_ == AV_CODEC_ID_H264) {
        av_opt_set_int(codec_ctx_->priv_data, "tune", 0 /* zerolatency */, 0);
    }
    
    // Enable low-latency options
    codec_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codec_ctx_->thread_type = FF_THREAD_SLICE;
    codec_ctx_->thread_count = 0;  // auto
    
    // Open codec
    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[VideoPipeline] Failed to open codec: " << errbuf << std::endl;
        avcodec_free_context(&codec_ctx_);
        return false;
    }
    
    // Create parser for extracting NALUs from byte stream
    parser_ctx_ = av_parser_init(codec_id_);
    if (!parser_ctx_) {
        std::cerr << "[VideoPipeline] Failed to create parser" << std::endl;
        // Parser is optional, can work without it for Annex B input
    }
    
    std::cout << "[VideoPipeline] Initialized with codec: " 
              << (codec_id_ == AV_CODEC_ID_H264 ? "H.264" : "H.265") << std::endl;
    
    return true;
}

void VideoPipeline::cleanup() {
    stop();
    
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    
    if (parser_ctx_) {
        av_parser_close(parser_ctx_);
        parser_ctx_ = nullptr;
    }
    
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    
    input_queue_.close();
    output_queue_.clear();
}

void VideoPipeline::push_rtp_data(const std::byte* data, size_t size) {
    VideoRTPPacket packet;
    packet.data.resize(size);
    memcpy(packet.data.data(), data, size);
    packet.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    input_queue_.push(std::move(packet));
    
    if (tracker_) tracker_->record_point(LatencyPoint::WEBRTC_RECEIVED);
}

bool VideoPipeline::start() {
    if (running_.load()) return true;
    
    running_.store(true);
    decode_thread_ = std::make_unique<std::thread>(&VideoPipeline::decode_loop, this);
    
    std::cout << "[VideoPipeline] Started decoding thread" << std::endl;
    return true;
}

void VideoPipeline::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    input_queue_.close();
    
    if (decode_thread_ && decode_thread_->joinable()) {
        decode_thread_->detach();
    }
    decode_thread_.reset();
    
    std::cout << "[VideoPipeline] Stopped" << std::endl;
}

void VideoPipeline::decode_loop() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* decoded_frame = av_frame_alloc();
    
    if (!pkt || !decoded_frame) {
        std::cerr << "[VideoPipeline] Failed to allocate packet/frame" << std::endl;
        if (pkt) av_packet_free(&pkt);
        if (decoded_frame) av_frame_free(&decoded_frame);
        return;
    }
    
    while (running_.load()) {
        VideoRTPPacket rtp_packet;
        
        if (!input_queue_.pop(rtp_packet)) {
            break;  // Queue closed
        }
        
        if (tracker_) tracker_->record_point(LatencyPoint::DECODE_STARTED);
        
        // Feed data to FFmpeg parser/decoder
        int ret = feed_decoder(rtp_packet.data.data(), rtp_packet.data.size());
        
        if (ret < 0) {
            frames_dropped_.fetch_add(1);
            continue;
        }
        
        // Try to receive decoded frames
        while (receive_frame(decoded_frame)) {
            if (tracker_) tracker_->record_point(LatencyPoint::DECODE_COMPLETED);
            
            // Output to V4L2
            if (!output_frame(decoded_frame)) {
                frames_dropped_.fetch_add(1);
            } else {
                frames_decoded_.fetch_add(1);
                if (tracker_) tracker_->record_point(LatencyPoint::V4L2_WRITTEN);
            }
        }
    }
    
    av_packet_free(&pkt);
    av_frame_free(&decoded_frame);
    
    std::cout <<("[VideoPipeline] Decode thread exited. Stats: decoded=" + 
              std::to_string(frames_decoded_.load()) +
              ", dropped=" + std::to_string(frames_dropped_.load())) << std::endl;
}

int VideoPipeline::feed_decoder(const uint8_t* data, size_t size) {
    // The RTP depacketizer (H264RtpDepacketizer) already outputs complete
    // Annex B access units with 00 00 00 01 start codes.
    // Feed directly to decoder — no av_parser_parse2 needed.
    // (The parser was splitting/reassembling NALs across calls which caused
    //  SPS/PPS to be lost, producing "non-existing PPS" decode errors.)
    
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return -1;
    
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = static_cast<int>(size);
    
    int ret = avcodec_send_packet(codec_ctx_, pkt);
    av_packet_free(&pkt);
    
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[VideoPipeline] send_packet error: " << errbuf 
                  << " (ret=" << ret << ")" << std::endl;
        return ret;
    }
    
    return 0;
}

bool VideoPipeline::receive_frame(AVFrame* frame) {
    int ret = avcodec_receive_frame(codec_ctx_, frame);
    
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return false;  // No frame available yet
    }
    
    if (ret < 0) {
        return false;
    }
    
    last_timestamp_.store(frame->pts);
    return true;
}

bool VideoPipeline::output_frame(AVFrame* frame) {
    if (!v4l2_out_ || !v4l2_out_->is_open()) {
        return false;
    }
    
    // Check if we need to reconfigure V4L2 device (resolution change)
    if (frame->width != v4l2_out_->get_width() ||
        frame->height != v4l2_out_->get_height())
    {
        std::cout <<("[VideoPipeline] Resolution changed: " +
                  std::to_string(frame->width) + "x" + std::to_string(frame->height)) << std::endl;
        
        // Reconfigure V4L2 output
        std::string dev = v4l2_out_->get_device_path();
        v4l2_out_->close();
        if (!v4l2_out_->open(dev)) {
            std::cerr << "[VideoPipeline] Failed to reopen V4L2 device after resolution change" << std::endl;
            return false;
        }
        
        // Configure new format (RGB24 for V4L2 compatibility)
        if (!v4l2_out_->configure(frame->width, frame->height, V4L2_PIX_FMT_RGB24)) {
            std::cerr << "[VideoPipeline] Failed to configure V4L2: "
                      << frame->width << "x" << frame->height << std::endl;
            return false;
        }
        
        // Create/recreate scaler context for YUV→RGB conversion
        if (sws_ctx_) {
            sws_freeContext(sws_ctx_);
        }
        sws_ctx_ = sws_getContext(
            frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format),
            frame->width, frame->height,
            AV_PIX_FMT_BGR24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if (!sws_ctx_) {
            std::cerr << "[VideoPipeline] Failed to create sws context for "
                      << frame->width << "x" << frame->height
                      << " fmt=" << frame->format << std::endl;
            return false;
        }
        std::cout << "[VideoPipeline] SWS scaler ready: "
                  << av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format))
                  << " -> BGR24" << std::endl;
    }
    
    // Safety: don't proceed if sws context is not ready
    if (!sws_ctx_) {
        return false;
    }
    
    // Convert YUV to BGR24 for V4L2 output
    AVFrame* rgb_frame = av_frame_alloc();
    if (!rgb_frame) return false;
    
    rgb_frame->format = AV_PIX_FMT_BGR24;
    rgb_frame->width = frame->width;
    rgb_frame->height = frame->height;
    
    int ret = av_frame_get_buffer(rgb_frame, 32);
    if (ret < 0) {
        av_frame_free(&rgb_frame);
        return false;
    }
    
    ret = av_frame_make_writable(rgb_frame);
    if (ret < 0) {
        av_frame_free(&rgb_frame);
        return false;
    }
    
    // Perform color space conversion
    sws_scale(sws_ctx_,
              frame->data, frame->linesize, 0, frame->height,
              rgb_frame->data, rgb_frame->linesize);
    
    // Write converted frame to V4L2 (uses DQBUF/QBUF flow for reliability)
    bool success = v4l2_out_->write_frame(rgb_frame);
    
    av_frame_free(&rgb_frame);
    return success;
}

uint64_t VideoPipeline::get_frames_decoded() const {
    return frames_decoded_.load();
}

uint64_t VideoPipeline::get_frames_dropped() const {
    return frames_dropped_.load();
}
