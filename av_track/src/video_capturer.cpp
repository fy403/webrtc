#include "video_capturer.h"
#include "debug_utils.h"
#include "encoder.h"
#include "h264_encoder.h"
#include "h265_encoder.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <random>
#include <sstream>
#include <typeinfo>
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

#include "rtc/rtc.hpp"

// 错误处理函数替代 av_err2str
std::string av_error_string(int errnum) {
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
  return std::string(errbuf);
}

VideoCapturer::VideoCapturer(const std::string &device, bool debug_enabled,
                             const std::string &resolution, int framerate,
                             const std::string &video_format,
                             size_t decode_queue_capacity,
                             size_t encode_queue_capacity,
                             size_t send_queue_capacity)
    : Capture(debug_enabled, decode_queue_capacity, encode_queue_capacity, send_queue_capacity),
      device_(device), resolution_(resolution), framerate_(framerate),
      video_format_(video_format) {
  avdevice_register_all();

  // 检测是否为网络流输入（UDP/RTSP/SDP）
  is_udp_stream_ = (device_.substr(0, 6) == "udp://" ||
                    device_.substr(0, 7) == "rtsp://" ||
                    device_.find(".sdp") != std::string::npos);
}

VideoCapturer::~VideoCapturer() { stop(); }

// Inherited from Capture base class
// void VideoCapturer::set_track_callback(TrackCallback callback)

bool VideoCapturer::start() {
  AVInputFormat *input_format = nullptr;
  std::string device_path = device_;

  if (is_udp_stream_) {
    // 网络流模式（UDP/RTSP/SDP）：直接接收H.264编码的视频流
    bool is_rtsp = (device_.substr(0, 7) == "rtsp://");
    bool is_sdp = (device_.find(".sdp") != std::string::npos);

    if (is_rtsp) {
      std::cout << "RTSP stream mode detected: " << device_ << std::endl;
    } else if (is_sdp) {
      std::cout << "SDP file mode detected: " << device_ << std::endl;
    } else {
      std::cout << "UDP stream mode detected: " << device_ << std::endl;
    }

    AVDictionary *options = nullptr;
    // 设置协议白名单
    av_dict_set(&options, "protocol_whitelist", "file,crypto,data,rtp,rtsp,udp,tcp", 0);

    if (is_rtsp) {
      // RTSP 特定配置
      av_dict_set(&options, "max_delay", "500000", 0);      // 0.5秒延迟
      av_dict_set(&options, "reorder_queue_size", "0", 0); // 禁用重排序队列
      av_dict_set(&options, "buffer_size", "1024000", 0);  // 1MB接收缓冲区
      av_dict_set(&options, "rtsp_transport", "tcp", 0);   // 使用TCP传输（更稳定）
      av_dict_set(&options, "stimeout", "5000000", 0);     // 5秒超时
    } else if (is_sdp) {
      // SDP 文件特定配置
      av_dict_set(&options, "max_delay", "500000", 0);      // 0.5秒延迟
      av_dict_set(&options, "probesize", "10000000", 0);   // 探测10MB数据
      av_dict_set(&options, "analyzeduration", "10000000", 0); // 分析10秒
      av_dict_set(&options, "fflags", "+genpts+discardcorrupt", 0);
    } else {
      // UDP 特定配置
      av_dict_set(&options, "max_delay", "500000", 0);      // 0.5秒延迟
      av_dict_set(&options, "reorder_queue_size", "0", 0); // 禁用重排序队列
      av_dict_set(&options, "buffer_size", "1024000", 0);  // 1MB接收缓冲区
    }

    int ret = avformat_open_input(&format_context_, device_path.c_str(),
                                  nullptr, &options);
    if (ret < 0) {
      std::cerr << "Cannot open network stream: " << av_error_string(ret) << std::endl;
      std::cerr << "Stream URL: " << device_path << std::endl;
      return false;
    }
  } else {
    // 普通摄像头模式
    input_format = av_find_input_format("v4l2");
    if (!input_format) {
      std::cerr << "Cannot find V4L2 input input_format" << std::endl;
      return false;
    }

    // Parse resolution
    int width = 640, height = 480; // Default values
    sscanf(resolution_.c_str(), "%dx%d", &width, &height);
    std::cout << "Resolution: " << width << "x" << height << std::endl;
    if (width < height) {
      resolution_ = std::to_string(height) + "x" + std::to_string(width);
    }

    AVDictionary *options = nullptr;
    av_dict_set(&options, "video_size", resolution_.c_str(), 0);
    av_dict_set(&options, "framerate", std::to_string(framerate_).c_str(), 0);
    av_dict_set(&options, "input_format", video_format_.c_str(),
                0); // 使用视频输入格式参数
    // 移除 pixel_format 设置，让 ffmpeg 自动检测
    std::cout << "Using video input format: " << video_format_ << std::endl;
    int ret = avformat_open_input(&format_context_, device_path.c_str(),
                                  input_format, &options);
    if (ret < 0) {
      std::cerr << "Cannot open video device: " << av_error_string(ret)
                << std::endl;
      return false;
    }
  }

  int ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot find stream info: " << av_error_string(ret)
              << std::endl;
    if (is_udp_stream_) {
      std::cerr << "Network stream may not be transmitting or connection failed" << std::endl;
    }
    return false;
  }

  video_stream_index_ = -1;
  for (unsigned int i = 0; i < format_context_->nb_streams; i++) {
    if (format_context_->streams[i]->codecpar->codec_type ==
        AVMEDIA_TYPE_VIDEO) {
      video_stream_index_ = i;
      break;
    }
  }

  if (video_stream_index_ == -1) {
    std::cerr << "Cannot find video stream" << std::endl;
    return false;
  }

  if (is_udp_stream_) {
    // UDP流模式：验证视频编码是否为H.264或H.265
    AVCodecParameters *codec_params =
        format_context_->streams[video_stream_index_]->codecpar;

    if (video_codec_ == "h264") {
      if (codec_params->codec_id != AV_CODEC_ID_H264) {
        std::cerr << "UDP stream codec is not H.264 (codec_id: " << codec_params->codec_id << ")" << std::endl;
        std::cerr << "Requested codec: h264" << std::endl;
        return false;
      }
      std::cout << "UDP stream is H.264 encoded, ready for direct forwarding" << std::endl;
    } else if (video_codec_ == "h265") {
      if (codec_params->codec_id != AV_CODEC_ID_H265) {
        std::cerr << "UDP stream codec is not H.265 (codec_id: " << codec_params->codec_id << ")" << std::endl;
        std::cerr << "Requested codec: h265" << std::endl;
        return false;
      }
      std::cout << "UDP stream is H.265 encoded, ready for direct forwarding" << std::endl;
    } else {
      std::cerr << "Unknown video codec: " << video_codec_ << std::endl;
      return false;
    }
  } else {
    // 普通摄像头模式：需要解码和编码
    AVCodecParameters *codec_params =
        format_context_->streams[video_stream_index_]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
      std::cerr << "Cannot find decoder" << std::endl;
      return false;
    }

    codec_context_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context_, codec_params);

    ret = avcodec_open2(codec_context_, codec, nullptr);
    if (ret < 0) {
      std::cerr << "Cannot open codec: " << av_error_string(ret) << std::endl;
      return false;
    }

    // 设置4个线程用于解码
    codec_context_->thread_count = 2;

    std::cout << "Capturer Decoder Using " << codec_context_->thread_count << " threads"
              << std::endl;

    // Parse resolution
    int width = 640, height = 480;
    sscanf(resolution_.c_str(), "%dx%d", &width, &height);

    // 根据视频编码器类型创建编码器
    if (video_codec_ == "h264") {
      encoder_ = std::make_unique<H264Encoder>(debug_enabled_);
      std::cout << "Using H.264 encoder" << std::endl;
    } else if (video_codec_ == "h265") {
      encoder_ = std::make_unique<H265Encoder>(debug_enabled_);
      std::cout << "Using H.265 encoder" << std::endl;
    } else {
      std::cerr << "Unknown video codec: " << video_codec_ << ", falling back to H.264" << std::endl;
      encoder_ = std::make_unique<H264Encoder>(debug_enabled_);
      video_codec_ = "h264";
    }

    // Initialize encoder
    if (!encoder_->open_encoder(width, height, framerate_, 0)) {
      std::cerr << "Cannot open " << video_codec_ << " encoder" << std::endl;
      return false;
    }
    AVCodecContext *encoder_context = encoder_->get_context();
    encoder_out_width_ = encoder_context->width;
    encoder_out_height_ = encoder_context->height;
    encoder_out_pix_fmt_ = encoder_context->pix_fmt;
    sws_context_ = sws_getContext(
        codec_context_->width, codec_context_->height, codec_context_->pix_fmt,
        encoder_out_width_, encoder_out_height_, encoder_out_pix_fmt_,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_context_) {
      std::cerr << "Cannot create SwsContext" << std::endl;
      return false;
    }
  }

  is_running_ = true;
  // 等待 track_callback_ 设置后再启动采集线程
  capture_thread_ = std::thread(&VideoCapturer::capture_loop, this);

  if (is_udp_stream_) {
    // 网络流模式（UDP/RTSP/SDP）：只启动采集和发送线程
    bool is_rtsp = (device_.substr(0, 7) == "rtsp://");
    bool is_sdp = (device_.find(".sdp") != std::string::npos);

    if (is_rtsp) {
      std::cout << "RTSP stream mode: Starting capture and send threads only" << std::endl;
    } else if (is_sdp) {
      std::cout << "SDP file mode: Starting capture and send threads only" << std::endl;
    } else {
      std::cout << "UDP stream mode: Starting capture and send threads only" << std::endl;
    }
    send_thread_ = std::thread(&VideoCapturer::send_loop, this);

    // 如果CPU数量大于等于2，则绑定线程到不同的CPU
    int cpu_count = get_cpu_count();
    if (cpu_count >= 2) {
      std::cout << "Detected " << cpu_count << " CPUs, binding threads to different CPUs" << std::endl;
      bind_thread_to_cpu(capture_thread_, 0); // 采集线程绑定到CPU 0
      bind_thread_to_cpu(send_thread_, 1);    // 发送线程绑定到CPU 1
    } else {
      std::cout << "CPU count: " << cpu_count << ", skipping CPU binding" << std::endl;
    }
  } else {
    // 普通摄像头模式：启动所有线程
    decode_thread_ = std::thread(&VideoCapturer::decode_loop, this);
    encode_thread_ = std::thread(&VideoCapturer::encode_loop, this);
    send_thread_ = std::thread(&VideoCapturer::send_loop, this);

    // 如果CPU数量大于等于4，则绑定线程到不同的CPU
    int cpu_count = get_cpu_count();
    if (cpu_count >= 4) {
      std::cout << "Detected " << cpu_count << " CPUs, binding threads to different CPUs" << std::endl;
      bind_thread_to_cpu(capture_thread_, 0); // 采集线程绑定到CPU 0
      //    bind_thread_to_cpu(decode_thread_, 3);  // 解码线程绑定到CPU 3
      //    bind_thread_to_cpu(encode_thread_, 1);  // 编码线程绑定到CPU 1
      bind_thread_to_cpu(send_thread_, 2);    // 发送线程绑定到CPU 2
    } else {
      std::cout << "CPU count: " << cpu_count << ", skipping CPU binding" << std::endl;
    }
  }

  std::cout << "Video capture started successfully" << std::endl;
  return true;
}

void VideoCapturer::stop() {
  Capture::stop();

  if (sws_context_) {
    sws_freeContext(sws_context_);
    sws_context_ = nullptr;
  }

  clear_frame_pool();

  if (codec_context_) {
    avcodec_free_context(&codec_context_);
    codec_context_ = nullptr;
  }

  if (format_context_) {
    avformat_close_input(&format_context_);
    format_context_ = nullptr;
  }
}

void VideoCapturer::set_video_codec(const std::string &codec) {
  video_codec_ = codec;
  std::cout << "Video codec set to: " << video_codec_ << std::endl;
}

void VideoCapturer::reconfigure(const std::string &resolution, int fps, int bitrate, const std::string &format) {
  std::cout << "Reconfiguring video capturer..." << std::endl;

  // 暂停采集
  pause_capture();

  // 清空队列
  decode_queue_.clear();
  encode_queue_.clear();
  send_queue_.clear();

  // 关闭旧编码器
  encoder_->close_encoder();

  // 清理旧资源
  if (sws_context_) {
    sws_freeContext(sws_context_);
    sws_context_ = nullptr;
  }

  if (codec_context_) {
    avcodec_free_context(&codec_context_);
    codec_context_ = nullptr;
  }

  if (format_context_) {
    avformat_close_input(&format_context_);
    format_context_ = nullptr;
  }

  // 清空帧池
  clear_frame_pool();

  // 更新参数
  resolution_ = resolution;
  framerate_ = fps;
  video_format_ = format;

  // 重新打开输入设备
  AVInputFormat *input_format = av_find_input_format("v4l2");
  if (!input_format) {
    std::cerr << "Cannot find V4L2 input format" << std::endl;
    return;
  }

  std::string device_path = device_;

  // 解析分辨率
  int width = 640, height = 480;
  sscanf(resolution_.c_str(), "%dx%d", &width, &height);
  if (width < height) {
    resolution_ = std::to_string(height) + "x" + std::to_string(width);
  }

  AVDictionary *options = nullptr;
  av_dict_set(&options, "video_size", resolution_.c_str(), 0);
  av_dict_set(&options, "framerate", std::to_string(framerate_).c_str(), 0);
  av_dict_set(&options, "input_format", video_format_.c_str(), 0);

  std::cout << "Using video input format: " << video_format_ << std::endl;

  int ret = avformat_open_input(&format_context_, device_path.c_str(),
                                input_format, &options);
  if (ret < 0) {
    std::cerr << "Cannot open video device: " << av_error_string(ret) << std::endl;
    return;
  }

  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot find stream info: " << av_error_string(ret) << std::endl;
    return;
  }

  video_stream_index_ = -1;
  for (unsigned int i = 0; i < format_context_->nb_streams; i++) {
    if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_index_ = i;
      break;
    }
  }

  if (video_stream_index_ == -1) {
    std::cerr << "Cannot find video stream" << std::endl;
    return;
  }

  AVCodecParameters *codec_params = format_context_->streams[video_stream_index_]->codecpar;
  const AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
  if (!codec) {
    std::cerr << "Cannot find decoder" << std::endl;
    return;
  }

  codec_context_ = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codec_context_, codec_params);

  ret = avcodec_open2(codec_context_, codec, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot open codec: " << av_error_string(ret) << std::endl;
    return;
  }

  codec_context_->thread_count = 2;

  // 使用新参数配置编码器
  if (!encoder_->open_encoder(width, height, fps, bitrate)) {
    std::cerr << "Failed to reconfigure encoder" << std::endl;
    return;
  }

  AVCodecContext *encoder_context = encoder_->get_context();
  if (!encoder_context) {
    std::cerr << "Encoder context is null after reconfiguration" << std::endl;
    return;
  }
  encoder_out_width_ = encoder_context->width;
  encoder_out_height_ = encoder_context->height;
  encoder_out_pix_fmt_ = encoder_context->pix_fmt;

  // 重新创建 SwsContext
  sws_context_ = sws_getContext(
      codec_context_->width, codec_context_->height, codec_context_->pix_fmt,
      encoder_out_width_, encoder_out_height_, encoder_out_pix_fmt_,
      SWS_BILINEAR, nullptr, nullptr, nullptr);

  if (!sws_context_) {
    std::cerr << "Cannot recreate SwsContext after reconfigure" << std::endl;
    return;
  }

  // 恢复采集
  resume_capture();

  std::cout << "Video capturer reconfigured successfully" << std::endl;
}

void VideoCapturer::capture_loop() {
  AVPacket *packet = av_packet_alloc();

  if (is_udp_stream_) {
    // 网络流模式（UDP/RTSP/SDP）：直接转发H.264数据包到发送队列
    bool is_rtsp = (device_.substr(0, 7) == "rtsp://");
    bool is_sdp = (device_.find(".sdp") != std::string::npos);

    if (is_rtsp) {
      std::cout << "RTSP capture loop started in direct forwarding mode" << std::endl;
    } else if (is_sdp) {
      std::cout << "SDP file capture loop started in direct forwarding mode" << std::endl;
    } else {
      std::cout << "UDP capture loop started in direct forwarding mode" << std::endl;
    }

    // 等待 track_callback_ 被设置
    {
      std::unique_lock<std::mutex> lock(callback_mutex_);
      callback_cv_.wait(lock, [this] { return track_callback_ || !is_running_; });
    }

    if (!is_running_) {
      av_packet_free(&packet);
      return;
    }

    while (is_running_) {
      // 检查是否暂停
      if (is_paused_) {
        std::unique_lock<std::mutex> lock(callback_mutex_);
        callback_cv_.wait(lock, [this] { return !is_paused_ || !is_running_; });
        continue;
      }

      int ret = av_read_frame(format_context_, packet);
      if (ret < 0) {
        if (ret != AVERROR(EAGAIN)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        continue;
      }

      if (packet->stream_index == video_stream_index_) {
        // 直接将H.264数据包放入发送队列
        AVPacket *clone_packet = av_packet_alloc();
        av_packet_ref(clone_packet, packet);

        if (!send_queue_.try_push(clone_packet)) {
          if (debug_enabled_) {
            std::cout << "UDP stream: Send queue full, dropping packet" << std::endl;
            std::cout << "Send queue Len: " << send_queue_.size()
                      << ", Capacity: " << send_queue_.capacity() << std::endl;
          }
          av_packet_free(&clone_packet);
          send_queue_.clear();
        }
      }

      av_packet_unref(packet);
    }
  } else {
    // 普通摄像头模式：需要解码和编码
    std::cout << "Camera capture loop started" << std::endl;

    // 计算实际采集/编码 FPS，避免只使用 num 导致不准确
    int encoder_out_fps = 0;
    if (encoder_->get_context()->framerate.den != 0) {
      encoder_out_fps = encoder_->get_context()->framerate.num /
                        encoder_->get_context()->framerate.den;
    }
    AVRational avg_rate =
        format_context_->streams[video_stream_index_]->avg_frame_rate;
    int capture_in_fps = 0;
    if (avg_rate.den != 0) {
      capture_in_fps = avg_rate.num / avg_rate.den;
    }

    if (encoder_out_fps <= 0) {
      encoder_out_fps = framerate_;
    }
    if (capture_in_fps <= 0) {
      capture_in_fps = encoder_out_fps;
    }

    int frame_drop_factor = 1; // 默认值，可以根据需要调整
    if (encoder_out_fps < capture_in_fps) {
      frame_drop_factor =
          std::max(1, static_cast<int>(std::round(static_cast<double>(capture_in_fps) /
                                                  static_cast<double>(encoder_out_fps))));
    }
    int frame_counter = 0;
    std::cout << "Capture FPS: " << capture_in_fps
              << ", Encode FPS: " << encoder_out_fps
              << ", Frame Drop Factor: " << frame_drop_factor << std::endl;

    // 等待 track_callback_ 被设置
    {
      std::unique_lock<std::mutex> lock(callback_mutex_);
      callback_cv_.wait(lock, [this] { return track_callback_ || !is_running_; });
    }

    if (!is_running_) {
      av_packet_free(&packet);
      return;
    }

    while (is_running_) {
      // 检查是否暂停
      if (is_paused_) {
        // 等待 track_callback_ 被设置或恢复采集
        std::unique_lock<std::mutex> lock(callback_mutex_);
        callback_cv_.wait(lock, [this] { return !is_paused_ || !is_running_; });
        continue;
      }

      int ret = av_read_frame(format_context_, packet);
      if (ret < 0) {
        if (ret != AVERROR(EAGAIN)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        continue;
      }

      if (packet->stream_index == video_stream_index_) {
        // 帧率控制逻辑：根据frame_drop_factor决定是否丢弃帧
        frame_counter++;
        if (frame_drop_factor > 1 && (frame_counter % frame_drop_factor) != 1) {
          // 跳过这一帧（不进行解码）
          av_packet_unref(packet);
          continue;
        }

        // 将数据包放入解码队列
        AVPacket *clone_packet = av_packet_alloc();
        av_packet_ref(clone_packet, packet);

        // 使用非阻塞方式推入队列
        if (!decode_queue_.try_push(clone_packet)) {
          if (debug_enabled_) {
            std::cout << "Video Decode queue full, dropping packet" << std::endl;
            std::cout << "Video Decode queue Len: " << decode_queue_.size()
                      << ", Capacity: " << decode_queue_.capacity()
                      << std::endl;
          }
          av_packet_free(&clone_packet);
          decode_queue_.clear();
          encode_queue_.clear();
          send_queue_.clear();
        }
      }

      av_packet_unref(packet);
    }
  }

  av_packet_free(&packet);
  std::cout << "Video capture stopped" << std::endl;
}

void VideoCapturer::decode_loop() {
  AVFrame *frame = av_frame_alloc();

  while (is_running_) {
    AVPacket *packet = nullptr;

    decode_queue_.wait_pop(packet);

    // nullptr 作为结束标记，方便线程在 stop() 时优雅退出
    if (!packet) {
      if (!is_running_) {
        break;
      }
      continue;
    }

    int ret = avcodec_send_packet(codec_context_, packet);
    if (ret < 0) {
      if (debug_enabled_) {
        std::cerr << "avcodec_send_packet failed: " << av_error_string(ret)
                  << std::endl;
      }
      av_packet_free(&packet);
      continue;
    }

    while (ret >= 0) {
      ret = avcodec_receive_frame(codec_context_, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      } else if (ret < 0) {
        if (debug_enabled_) {
          std::cerr << "avcodec_receive_frame failed: " << av_error_string(ret)
                    << std::endl;
        }
        break;
      }

      // 保存前几帧用于调试
      if (debug_enabled_) {
        static int saved_count = 0;
        if (saved_count < 5) {
          std::stringstream filename;
          filename << "captured_frame_" << saved_count << "_" << frame->width
                   << "x" << frame->height << ".ppm";
          DebugUtils::save_frame_to_ppm(frame, filename.str());

          std::stringstream yuv_filename;
          yuv_filename << "captured_frame_" << saved_count << ".yuv";
          DebugUtils::save_frame_to_yuv(frame, yuv_filename.str());

          saved_count++;
        }
      }

      // 根据实际帧尺寸/像素格式检测是否需要重新创建 sws_context_
      if (!sws_context_ || frame->width != codec_context_->width ||
          frame->height != codec_context_->height ||
          frame->format != codec_context_->pix_fmt) {
        if (sws_context_) {
          sws_freeContext(sws_context_);
        }
        sws_context_ = sws_getContext(
            frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
            encoder_out_width_, encoder_out_height_, encoder_out_pix_fmt_,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_context_) {
          std::cerr << "Cannot recreate SwsContext for frame " << frame->width
                    << "x" << frame->height << std::endl;
          continue;
        }
      }

      // Convert frame format and put in encode queue using frame pool
      AVFrame *scaled_frame = acquire_scaled_frame();
      if (!scaled_frame) {
        // 如果无法从池中获取，直接跳过本帧，避免崩溃
        if (debug_enabled_) {
          std::cerr << "Failed to acquire scaled frame from pool, dropping frame"
                    << std::endl;
        }
        continue;
      }

      sws_scale(sws_context_, frame->data, frame->linesize, 0, frame->height,
                scaled_frame->data, scaled_frame->linesize);

      scaled_frame->pts = frame->pts;

      // encode_queue_.wait_push(scaled_frame);
      // 使用非阻塞方式推入队列
      if (!encode_queue_.try_push(scaled_frame)) {
        if (debug_enabled_) {
          std::cout << "Video Encode queue full, dropping audio frame"
                    << std::endl;
          std::cout << "Video Encode queue Len: " << encode_queue_.size()
                    << ", Capacity: " << encode_queue_.capacity()
                    << std::endl;
        }
        release_scaled_frame(scaled_frame);
        encode_queue_.clear();
        send_queue_.clear();
      }
    }

    av_packet_free(&packet);
  }

  av_frame_free(&frame);
  std::cout << "Video Decode thread exiting" << std::endl;
}

void VideoCapturer::encode_loop() {
  AVPacket *packet = av_packet_alloc();

  while (is_running_) {
    AVFrame *frame = nullptr;

    encode_queue_.wait_pop(frame);

    // nullptr 作为结束标记
    if (!frame) {
      if (!is_running_) {
        break;
      }
      continue;
    }

    bool encoded = encoder_->encode_frame(frame, packet);
    // 使用 frame pool 复用内存
    release_scaled_frame(frame);

    if (encoded) {
      // Put packet in send queue
      // Clone packet for sending thread
      AVPacket *clone_packet = av_packet_alloc();
      av_packet_ref(clone_packet, packet);
        send_queue_.wait_push(clone_packet);

      av_packet_unref(packet);
    }
  }

  av_packet_free(&packet);
  std::cout << "Video Encode thread exiting" << std::endl;
}

void VideoCapturer::send_loop() {
  while (is_running_) {
    AVPacket *packet = nullptr;

    // Get packet from send queue
    send_queue_.wait_pop(packet);

    // nullptr 作为结束标记
    if (!packet) {
      if (!is_running_) {
        break;
      }
      continue;
    }

    if (!is_running_) {
      av_packet_free(&packet);
      break;
    }

    // Send data using callback
    if (track_callback_) {
      auto data = reinterpret_cast<const std::byte *>(packet->data);
      track_callback_(data, packet->size);
    } else {
      std::cout << "Drop packet! No callback set." << std::endl;
    }

    av_packet_free(&packet);
  }
  std::cout << "Video Send thread exiting" << std::endl;
}

AVFrame *VideoCapturer::acquire_scaled_frame() {
  std::lock_guard<std::mutex> lock(frame_pool_mutex_);
  AVFrame *frame = nullptr;
  if (!scaled_frame_pool_.empty()) {
    frame = scaled_frame_pool_.back();
    scaled_frame_pool_.pop_back();
  } else {
    frame = av_frame_alloc();
    if (!frame) {
      return nullptr;
    }
    frame->format = encoder_out_pix_fmt_;
    frame->width = encoder_out_width_;
    frame->height = encoder_out_height_;
    if (av_frame_get_buffer(frame, 0) < 0) {
      av_frame_free(&frame);
      return nullptr;
    }
  }
  // 确保数据可写
  if (av_frame_make_writable(frame) < 0) {
    return nullptr;
  }
  return frame;
}

void VideoCapturer::release_scaled_frame(AVFrame *frame) {
  if (!frame) {
    return;
  }
  std::lock_guard<std::mutex> lock(frame_pool_mutex_);
  // 简单限制池大小，避免无限增长
  constexpr size_t kMaxPoolSize = 32;
  if (scaled_frame_pool_.size() < kMaxPoolSize) {
    scaled_frame_pool_.push_back(frame);
  } else {
    av_frame_free(&frame);
  }
}

void VideoCapturer::clear_frame_pool() {
  std::lock_guard<std::mutex> lock(frame_pool_mutex_);
  for (auto *frame : scaled_frame_pool_) {
    av_frame_free(&frame);
  }
  scaled_frame_pool_.clear();
}