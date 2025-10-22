#include "video_capturer.h"
#include "debug_utils.h"
#include <iostream>
#include <functional>
#include <chrono>
#include <future>
#include <cstring>
#include <random>
#include <algorithm>
#include <sstream>
extern "C"
{
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

#include "rtc/rtc.hpp"

// 错误处理函数替代 av_err2str
std::string av_error_string(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return std::string(errbuf);
}

VideoCapturer::VideoCapturer(const std::string &device, bool debug_enabled,
                             size_t encode_queue_capacity, size_t send_queue_capacity)
    : device_(device), debug_enabled_(debug_enabled), is_running_(false), is_capturing_(false), is_paused_(false),
      encode_queue_(encode_queue_capacity), send_queue_(send_queue_capacity)
{
    avdevice_register_all();
}

VideoCapturer::~VideoCapturer()
{
    stop();
}

void VideoCapturer::set_track_callback(TrackCallback callback)
{
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        track_callback_ = std::move(callback);
    }
    callback_cv_.notify_one();
}

bool VideoCapturer::start()
{
    AVInputFormat *input_format = av_find_input_format("v4l2");
    if (!input_format)
    {
        std::cerr << "Cannot find V4L2 input format" << std::endl;
        return false;
    }
    std::string device_path = device_;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "video_size", "1280x720", 0);
    av_dict_set(&options, "framerate", "15", 0);
    av_dict_set(&options, "input_format", "mjpeg", 0); // 关键修改
    // 移除 pixel_format 设置，让 ffmpeg 自动检测
    int ret = avformat_open_input(&format_context_, device_path.c_str(), input_format, &options);
    if (ret < 0)
    {
        std::cerr << "Cannot open video device: " << av_error_string(ret) << std::endl;
        return false;
    }

    ret = avformat_find_stream_info(format_context_, nullptr);
    if (ret < 0)
    {
        std::cerr << "Cannot find stream info: " << av_error_string(ret) << std::endl;
        return false;
    }

    video_stream_index_ = -1;
    for (unsigned int i = 0; i < format_context_->nb_streams; i++)
    {
        if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index_ = i;
            break;
        }
    }

    if (video_stream_index_ == -1)
    {
        std::cerr << "Cannot find video stream" << std::endl;
        return false;
    }

    AVCodecParameters *codec_params = format_context_->streams[video_stream_index_]->codecpar;
    // 在 start() 函数中找到解码器后添加
    // if (codec_params->codec_id == AV_CODEC_ID_MJPEG)
    // {
    //     // MJPEG 解码器可能需要特殊处理
    //     codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // }
    const AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec)
    {
        std::cerr << "Cannot find decoder" << std::endl;
        return false;
    }

    codec_context_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context_, codec_params);

    ret = avcodec_open2(codec_context_, codec, nullptr);
    if (ret < 0)
    {
        std::cerr << "Cannot open codec: " << av_error_string(ret) << std::endl;
        return false;
    }

    // 在 video_capturer.cpp 的 start() 函数中修改编码器配置
    const AVCodec *h264_codec = avcodec_find_encoder_by_name("libx264");
    if (!h264_codec)
    {
        h264_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!h264_codec)
        {
            std::cerr << "Cannot find H.264 encoder" << std::endl;
            return false;
        }
    }

    encoder_context_ = avcodec_alloc_context3(h264_codec);

    // 基本配置
    encoder_context_->width = 640;
    encoder_context_->height = 480;
    encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_context_->time_base = {1, 30};
    encoder_context_->framerate = {30, 1};

    // 更频繁的关键帧
    encoder_context_->gop_size = 15;   // 每15帧一个关键帧，确保快速启动
    encoder_context_->keyint_min = 15; // 最小关键帧间隔

    // 禁用 B 帧，确保低延迟和兼容性
    encoder_context_->max_b_frames = 0;
    encoder_context_->has_b_frames = 0;

    // 码率控制
    encoder_context_->bit_rate = 800000;
    encoder_context_->rc_max_rate = 800000;
    encoder_context_->rc_buffer_size = 800000;

    // 强制使用 baseline profile
    encoder_context_->profile = FF_PROFILE_H264_BASELINE;
    encoder_context_->level = 31;

    // 设置编码器参数
    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);

    std::cout << "Encoder configured with GOP size: " << encoder_context_->gop_size << std::endl;

    ret = avcodec_open2(encoder_context_, h264_codec, nullptr);
    if (ret < 0)
    {
        std::cerr << "Cannot open H.264 encoder: " << av_error_string(ret) << std::endl;
        return false;
    }

    // 检查编码器是否支持全局头
    std::cout << "Encoder supports global header: " << (encoder_context_->flags & AV_CODEC_FLAG_GLOBAL_HEADER) << std::endl;

    sws_context_ = sws_getContext(
        codec_context_->width, codec_context_->height, codec_context_->pix_fmt,
        encoder_context_->width, encoder_context_->height, encoder_context_->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_context_)
    {
        std::cerr << "Cannot create SwsContext" << std::endl;
        return false;
    }

    is_running_ = true;
    // 等待 track_callback_ 设置后再启动采集线程
    capture_thread_ = std::thread(&VideoCapturer::capture_loop, this);
    encode_thread_ = std::thread(&VideoCapturer::encode_loop, this);
    send_thread_ = std::thread(&VideoCapturer::send_loop, this);

    std::cout << "Video capture started successfully" << std::endl;
    return true;
}

void VideoCapturer::stop()
{
    is_running_ = false;
    is_capturing_ = false;
    is_paused_ = false;

    // Notify all waiting threads
    encode_queue_.clear();
    send_queue_.clear();
    // Push sentinel nullptrs to unblock waiters
    encode_queue_.push(nullptr);
    send_queue_.push(nullptr);

    // Notify condition variables to wake up waiting threads
    callback_cv_.notify_all();

    if (capture_thread_.joinable())
    {
        capture_thread_.join();
    }

    if (encode_thread_.joinable())
    {
        encode_thread_.join();
    }

    if (send_thread_.joinable())
    {
        send_thread_.join();
    }

    // Clean up queues
    while (!encode_queue_.empty())
    {
        AVFrame *frame;
        if (encode_queue_.pop(frame))
        {
            av_frame_free(&frame);
        }
    }

    while (!send_queue_.empty())
    {
        AVPacket *packet;
        if (send_queue_.pop(packet))
        {
            av_packet_free(&packet);
        }
    }

    if (sws_context_)
    {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
    }

    if (encoder_context_)
    {
        avcodec_free_context(&encoder_context_);
        encoder_context_ = nullptr;
    }

    if (codec_context_)
    {
        avcodec_free_context(&codec_context_);
        codec_context_ = nullptr;
    }

    if (format_context_)
    {
        avformat_close_input(&format_context_);
        format_context_ = nullptr;
    }
}

void VideoCapturer::capture_loop()
{
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    static int saved_count = 0;

    // 等待 track_callback_ 被设置
    {
        std::unique_lock<std::mutex> lock(callback_mutex_);
        callback_cv_.wait(lock, [this]
                          { return track_callback_ || !is_running_; });
    }

    if (!is_running_)
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        return;
    }

    // 设置采集标志
    is_capturing_ = true;

    while (is_running_)
    {
        // 检查是否暂停
        if (is_paused_)
        {
            // 等待 track_callback_ 被设置或恢复采集
            std::unique_lock<std::mutex> lock(callback_mutex_);
            callback_cv_.wait(lock, [this]
                              { return !is_paused_ || !is_running_; });
            continue;
        }

        int ret = av_read_frame(format_context_, packet);
        if (ret < 0)
        {
            if (ret != AVERROR(EAGAIN))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (packet->stream_index == video_stream_index_)
        {
            ret = avcodec_send_packet(codec_context_, packet);
            if (ret < 0)
            {
                av_packet_unref(packet);
                continue;
            }

            while (ret >= 0)
            {
                ret = avcodec_receive_frame(codec_context_, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    break;
                }

                // 保存前几帧用于调试
                if (debug_enabled_)
                {
                    static int saved_count = 0;
                    if (saved_count < 5)
                    {
                        std::stringstream filename;
                        filename << "captured_frame_" << saved_count << "_"
                                 << frame->width << "x" << frame->height << ".ppm";
                        DebugUtils::save_frame_to_ppm(frame, filename.str());

                        std::stringstream yuv_filename;
                        yuv_filename << "captured_frame_" << saved_count << ".yuv";
                        DebugUtils::save_frame_to_yuv(frame, yuv_filename.str());

                        saved_count++;
                    }
                }

                // Convert frame format and put in encode queue
                AVFrame *scaled_frame = av_frame_alloc();
                scaled_frame->format = AV_PIX_FMT_YUV420P;
                scaled_frame->width = encoder_context_->width;
                scaled_frame->height = encoder_context_->height;

                av_frame_get_buffer(scaled_frame, 0);

                sws_scale(sws_context_,
                          frame->data, frame->linesize, 0, frame->height,
                          scaled_frame->data, scaled_frame->linesize);

                scaled_frame->pts = frame->pts;

                encode_queue_.push(scaled_frame);
            }
        }

        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
}

void VideoCapturer::encode_loop()
{
    AVPacket *packet = av_packet_alloc();

    while (is_running_)
    {
        AVFrame *frame = nullptr;

        // Get frame from encode queue
        encode_queue_.wait_and_pop(frame);

        if (!frame)
            continue;

        // Ensure frame has timestamp
        static int64_t pts = 0;
        if (frame->pts == AV_NOPTS_VALUE)
        {
            frame->pts = pts++;
        }

        int ret = avcodec_send_frame(encoder_context_, frame);
        av_frame_free(&frame);

        if (ret < 0)
        {
            continue;
        }

        while (ret >= 0)
        {
            ret = avcodec_receive_packet(encoder_context_, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                std::cerr << "Error receiving packet from encoder: " << av_error_string(ret) << std::endl;
                break;
            }

            if (debug_enabled_)
            {
                std::cout << "packet size: " << packet->size
                          << ", packet pts: " << packet->pts
                          << ", keyframe: " << (packet->flags & AV_PKT_FLAG_KEY) << std::endl;
                // 分析 NALU
                DebugUtils::analyze_nal_units(packet);
            }

            // Put packet in send queue
            // Clone packet for sending thread
            AVPacket *clone_packet = av_packet_alloc();
            av_packet_ref(clone_packet, packet);
            send_queue_.push(clone_packet);

            av_packet_unref(packet);
        }
    }

    av_packet_free(&packet);
}

void VideoCapturer::send_loop()
{
    while (is_running_)
    {
        AVPacket *packet = nullptr;

        // Get packet from send queue
        send_queue_.wait_and_pop(packet);

        if (!packet)
            continue;

        // Wait for callback to be set
        std::unique_lock<std::mutex> lock(callback_mutex_);
        callback_cv_.wait(lock, [this]
                          { return track_callback_ || !is_running_; });

        if (!is_running_)
        {
            av_packet_free(&packet);
            break;
        }

        // Send data using callback
        if (track_callback_)
        {
            auto data = reinterpret_cast<const std::byte *>(packet->data);
            track_callback_(data, packet->size);
        }
        else
        {
            std::cout << "Drop packet! No callback set." << std::endl;
        }

        av_packet_free(&packet);
    }
}

void VideoCapturer::pause_capture()
{
    is_paused_ = true;

    // 清空队列
    encode_queue_.clear();
    send_queue_.clear();

    std::cout << "Video capture paused and queues cleared" << std::endl;
}

void VideoCapturer::resume_capture()
{
    is_paused_ = false;
    // 通知等待的线程，采集已恢复
    callback_cv_.notify_all();
    std::cout << "Video capture resumed" << std::endl;
}
