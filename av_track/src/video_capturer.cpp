#include "video_capturer.h"
#include <iostream>
#include <functional>
#include <chrono>
#include <future>
#include <cstring>
#include <random>
#include <algorithm>

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

VideoCapturer::VideoCapturer(const std::string &device)
    : device_(device), is_running_(false)
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
#if defined(_WIN32) || defined(WIN32)
    AVInputFormat *input_format = av_find_input_format("dshow");
    if (!input_format)
    {
        std::cerr << "Cannot find dshow input format" << std::endl;
        return false;
    }

    // For Windows, we need to format the device name properly for DirectShow
    // The device name should be in the format "video=DEVICE_NAME"
    std::string device_path = device_;
    if (device_.substr(0, 6) != "video=")
    {
        device_path = "video=" + device_;
    }
#else
    AVInputFormat *input_format = av_find_input_format("v4l2");
    if (!input_format)
    {
        std::cerr << "Cannot find V4L2 input format" << std::endl;
        return false;
    }
    std::string device_path = device_;
#endif

    AVDictionary *options = nullptr;
    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "framerate", "30", 0);
#if defined(_WIN32) || defined(WIN32)
    av_dict_set(&options, "pixel_format", "uyvy422", 0); // DirectShow typically uses uyvy422
#else
    av_dict_set(&options, "pixel_format", "yuyv422", 0);
#endif

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

    // 初始化 H.264 编码器
    const AVCodec *h264_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!h264_codec)
    {
        std::cerr << "Cannot find H.264 encoder" << std::endl;
        return false;
    }

    encoder_context_ = avcodec_alloc_context3(h264_codec);
    encoder_context_->width = 320; // 降低分辨率
    encoder_context_->height = 240;
    encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_context_->time_base = {1, 30};
    encoder_context_->framerate = {30, 1};
    encoder_context_->gop_size = 10;
    encoder_context_->max_b_frames = 0; // 不要B帧

    // 在初始化编码器的地方添加以下参数
    encoder_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    encoder_context_->max_b_frames = 0;
    encoder_context_->gop_size = 30;
    encoder_context_->keyint_min = 30;

    // 设置码率控制参数来限制帧大小
    encoder_context_->bit_rate = 500000; // 500 kbps
    encoder_context_->rc_max_rate = 500000;
    encoder_context_->rc_buffer_size = 1000000;

    // 使用更严格的预设
    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(encoder_context_->priv_data, "crf", "32", 0); // 质量因子

    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);

    av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);

    ret = avcodec_open2(encoder_context_, h264_codec, nullptr);
    if (ret < 0)
    {
        std::cerr << "Cannot open H.264 encoder: " << av_error_string(ret) << std::endl;
        return false;
    }

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
    capture_thread_ = std::thread(&VideoCapturer::capture_loop, this);

    std::cout << "Video capture started successfully" << std::endl;
    return true;
}

void VideoCapturer::stop()
{
    is_running_ = false;
    if (capture_thread_.joinable())
    {
        capture_thread_.join();
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
    AVFrame *yuv_frame = av_frame_alloc();

    yuv_frame->format = AV_PIX_FMT_YUV420P;
    yuv_frame->width = 640;
    yuv_frame->height = 480;
    yuv_frame->pts = 0;

    int ret = av_frame_get_buffer(yuv_frame, 0);
    if (ret < 0)
    {
        std::cerr << "Cannot allocate frame buffer: " << av_error_string(ret) << std::endl;
        av_frame_free(&yuv_frame);
        av_frame_free(&frame);
        av_packet_free(&packet);
        return;
    }

    std::cout << "Starting video capture loop" << std::endl;

    while (is_running_)
    {
        if (!track_callback_)
        {
            // 使用条件变量等待回调函数被设置
            std::unique_lock<std::mutex> lock(callback_mutex_);
            callback_cv_.wait(lock, [this]
                              { return track_callback_ || !is_running_; });
            if (!is_running_)
            {
                break;
            }
            continue;
        }
        ret = av_read_frame(format_context_, packet);
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

                // std::cout << "Got frame" << std::endl;

                // 转换帧格式
                sws_scale(sws_context_,
                          frame->data, frame->linesize, 0, frame->height,
                          yuv_frame->data, yuv_frame->linesize);

                // 编码为 H.264
                encode_and_send(yuv_frame);

                yuv_frame->pts++;
            }
        }

        av_packet_unref(packet);
    }

    av_frame_free(&yuv_frame);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

void VideoCapturer::encode_and_send(AVFrame *frame)
{
    // 确保帧有时间戳
    if (frame->pts == AV_NOPTS_VALUE)
    {
        static int64_t pts = 0;
        frame->pts = pts++;
    }

    // 设置时间基
    frame->pkt_dts = frame->pts;
    frame->pkt_pts = frame->pts;
    frame->flags |= AV_PKT_FLAG_KEY;

    AVPacket *packet = av_packet_alloc();
    if (!packet)
        return;

    int ret = avcodec_send_frame(encoder_context_, frame);
    if (ret < 0)
    {
        av_packet_free(&packet);
        return;
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

        // 添加包信息调试
        std::cout << "Encoded packet: size=" << packet->size
                  << ", pts=" << packet->pts
                  << ", dts=" << packet->dts
                  << ", key_frame=" << (packet->flags & AV_PKT_FLAG_KEY)
                  << std::endl;

        // 使用回调发送数据
        if (track_callback_)
        {
            auto data = reinterpret_cast<const std::byte *>(packet->data);
            track_callback_(data, packet->size);
        }
        else
        {
            std::cout << "Drop packet! No callback set." << std::endl;
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);
}