#include "encoder.h"
#include "debug_utils.h"
#include <iostream>

extern "C"
{
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

extern std::string av_error_string(int errnum);

H264Encoder::H264Encoder(bool debug_enabled)
    : debug_enabled_(debug_enabled), encoder_context_(nullptr), codec_(nullptr)
{
}

H264Encoder::~H264Encoder()
{
    close_encoder();
}

bool H264Encoder::open_encoder(int width, int height, int fps)
{
    // 在 video_capturer.cpp 的 start() 函数中修改编码器配置
    codec_ = avcodec_find_encoder_by_name("libx264");
    if (!codec_)
    {
        codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec_)
        {
            std::cerr << "Cannot find H.264 encoder" << std::endl;
            return false;
        }
    }

    encoder_context_ = avcodec_alloc_context3(codec_);

    // 基本配置
    encoder_context_->width = width;
    encoder_context_->height = height;
    encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_context_->time_base = {1, fps};
    encoder_context_->framerate = {fps, 1};

    // 更频繁的关键帧
    encoder_context_->gop_size = 15;   // 每15帧一个关键帧，确保快速启动
    encoder_context_->keyint_min = 15; // 最小关键帧间隔

    // 禁用 B 帧，确保低延迟和兼容性
    encoder_context_->max_b_frames = 0;
    encoder_context_->has_b_frames = 0;

    // 码率控制：VBR运行较大波动
    // encoder_context_->bit_rate = 800000;
    // encoder_context_->rc_max_rate = 2500000;
    // encoder_context_->rc_buffer_size = 2500000;

    // 强制使用 baseline profile
    encoder_context_->profile = FF_PROFILE_H264_BASELINE;
    encoder_context_->level = 31;

    // 设置编码器参数
    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
    // av_opt_set(encoder_context_->priv_data, "crf", "32", 0);
    av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);

    std::cout << "Encoder configured with GOP size: " << encoder_context_->gop_size << std::endl;

    int ret = avcodec_open2(encoder_context_, codec_, nullptr);
    if (ret < 0)
    {
        std::cerr << "Cannot open H.264 encoder: " << av_error_string(ret) << std::endl;
        return false;
    }

    // 检查编码器是否支持全局头
    std::cout << "Encoder supports global header: " << (encoder_context_->flags & AV_CODEC_FLAG_GLOBAL_HEADER) << std::endl;

    return true;
}

void H264Encoder::close_encoder()
{
    if (encoder_context_)
    {
        avcodec_free_context(&encoder_context_);
        encoder_context_ = nullptr;
    }
}

bool H264Encoder::encode_frame(AVFrame *frame, AVPacket *packet)
{
    // Ensure frame has timestamp
    static int64_t pts = 0;
    if (frame && frame->pts == AV_NOPTS_VALUE)
    {
        frame->pts = pts++;
    }

    int ret = avcodec_send_frame(encoder_context_, frame);
    if (ret < 0)
    {
        return false;
    }

    ret = avcodec_receive_packet(encoder_context_, packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        return false;
    }
    else if (ret < 0)
    {
        std::cerr << "Error receiving packet from encoder: " << av_error_string(ret) << std::endl;
        return false;
    }

    if (debug_enabled_)
    {
        std::cout << "packet size: " << packet->size
                  << ", packet pts: " << packet->pts
                  << ", keyframe: " << (packet->flags & AV_PKT_FLAG_KEY) << std::endl;
        // 分析 NALU
        DebugUtils::analyze_nal_units(packet);
    }

    return true;
}