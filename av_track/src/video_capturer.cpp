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
#include <libavutil/time.h>
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
      video_format_(video_format), target_fps_(framerate) {
  avdevice_register_all();

  // 检测是否为模拟摄像头模式（lavfi）
  is_fake_camera_ = (device_ == "fake" || device_.substr(0, 5) == "lavfi");

  // 检测是否为网络流输入（UDP/RTSP/SDP）
  is_udp_stream_ = (device_.substr(0, 6) == "udp://" ||
                    device_.substr(0, 7) == "rtsp://" ||
                    device_.find(".sdp") != std::string::npos);
}

VideoCapturer::~VideoCapturer() { stop(); }

// Inherited from Capture base class
// void VideoCapturer::set_track_callback(TrackCallback callback)

bool VideoCapturer::start() {
  const AVInputFormat *input_format = nullptr;
  std::string device_path = device_;

  // 检测是否为模拟摄像头模式 (lavfi)
  bool is_fake_camera = (device_ == "fake" || device_.substr(0, 5) == "lavfi");

  if (is_fake_camera) {
    std::cout << "Fake camera mode (lavfi) enabled" << std::endl;

    // 解析分辨率
    int width = 640, height = 480;
    sscanf(resolution_.c_str(), "%dx%d", &width, &height);
    std::cout << "Fake camera resolution: " << width << "x" << height << std::endl;

    // 构建 lavfi 描述符：testsrc 生成测试图案
    // testsrc:   生成彩色测试图案
    // format:    指定像素格式为 yuv420p（编码器友好）
    char lavfi_desc[256];
    snprintf(lavfi_desc, sizeof(lavfi_desc),
             "testsrc=size=%dx%d:rate=%d,"
             "format=pix_fmts=yuv420p",
             width, height, framerate_);

    std::cout << "lavfi desc: " << lavfi_desc << std::endl;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "format_name", "lavfi", 0);

    int ret = avformat_open_input(&format_context_, lavfi_desc,
                                  av_find_input_format("lavfi"), &options);
    if (ret < 0) {
      std::cerr << "Cannot open lavfi device: " << av_error_string(ret) << std::endl;
      std::cerr << "Please ensure FFmpeg is compiled with lavfi support" << std::endl;
      return false;
    }

    // lavfi 输出原始帧，需要走 解码→编码 流程
    // 不设置 is_udp_stream_，让代码继续走下面的摄像头分支
    is_fake_camera_ = true;
  } else if (is_udp_stream_) {
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
      // RTSP 特定配置（低延时优化）
      av_dict_set(&options, "max_delay", "100000", 0);       // 100ms输入延迟
      av_dict_set(&options, "reorder_queue_size", "0", 0);       // 禁用重排序队列
      av_dict_set(&options, "buffer_size", "102400", 0);        // 100KB接收缓冲区
      av_dict_set(&options, "rtsp_transport", "tcp", 0);         // 使用TCP传输
      av_dict_set(&options, "stimeout", "5000000", 0);          // 5秒超时
    } else if (is_sdp) {
      // SDP 文件特定配置（低延时优化）
      av_dict_set(&options, "max_delay", "100000", 0);        // 100ms输入延迟
      av_dict_set(&options, "probesize", "10000000", 0);     // 探测10MB数据
      av_dict_set(&options, "analyzeduration", "10000000", 0); // 分析10秒
      av_dict_set(&options, "fflags", "+genpts+discardcorrupt", 0);
    } else {
      // UDP / 本地摄像头（低延时优化）
      av_dict_set(&options, "max_delay", "100000", 0);        // 100ms输入延迟
      av_dict_set(&options, "reorder_queue_size", "0", 0); // 禁用重排序队列
      av_dict_set(&options, "buffer_size", "102400", 0);        // 100KB接收缓冲区
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
    if (!video_format_.empty()) {
      av_dict_set(&options, "input_format", video_format_.c_str(), 0); // 使用视频输入格式参数
      std::cout << "Using video input format: " << video_format_ << std::endl;
    } else {
      std::cout << "Using video input format: auto-detect" << std::endl;
    }
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

    // 根据视频编码器配置选择编码器
    // 支持格式:
    //   h264        -> libx264 软编码
    //   h265        -> libx265 软编码
    //   h264_rkmpp  -> Rockchip H.264 硬编码
    //   hevc_rkmpp  -> Rockchip H.265(HEVC) 硬编码
    //   fake        -> 使用 libx264 编码测试图案
    //   其他        -> 直接作为 FFmpeg 编码器名称
    std::string encoder_name = video_codec_;
    if (video_codec_ == "h264" || video_codec_ == "fake") {
      encoder_name = "libx264";
      if (video_codec_ == "fake") {
        std::cout << "Fake mode: using libx264 for test pattern encoding" << std::endl;
      }
    } else if (video_codec_ == "h265") {
      encoder_name = "libx265";
    }

    // 通过编码器名称查找，自动判断 codec 类型
    const AVCodec *probe_codec = avcodec_find_encoder_by_name(encoder_name.c_str());
    if (!probe_codec) {
      std::cerr << "Cannot find encoder: " << encoder_name << ", falling back to libx264" << std::endl;
      encoder_name = "libx264";
      probe_codec = avcodec_find_encoder_by_name(encoder_name.c_str());
      if (!probe_codec) {
        std::cerr << "Cannot find fallback encoder: libx264" << std::endl;
        return false;
      }
    }

    std::cout << "Probe encoder: " << encoder_name << " -> codec_id=" << probe_codec->id
              << " (" << (probe_codec->id == AV_CODEC_ID_H264 ? "H.264" :
                         probe_codec->id == AV_CODEC_ID_H265 ? "H.265" : "unknown") << ")"
              << std::endl;

    if (probe_codec->id == AV_CODEC_ID_H264 || probe_codec->id == AV_CODEC_ID_H263P ||
        probe_codec->id == AV_CODEC_ID_MPEG4) {
      encoder_ = std::make_unique<H264Encoder>(debug_enabled_, encoder_name);
      video_codec_ = "h264";
      std::cout << "Using H.264 encoder: " << encoder_name << std::endl;
    } else if (probe_codec->id == AV_CODEC_ID_H265 || probe_codec->id == AV_CODEC_ID_HEVC) {
      encoder_ = std::make_unique<H265Encoder>(debug_enabled_, encoder_name);
      video_codec_ = "h265";
      std::cout << "Using H.265 encoder: " << encoder_name << std::endl;
    } else {
      std::cerr << "Unsupported codec_id: " << probe_codec->id << ", falling back to libx264" << std::endl;
      encoder_ = std::make_unique<H264Encoder>(debug_enabled_, "libx264");
      video_codec_ = "h264";
    }

    // Initialize encoder
    if (!encoder_->open_encoder(width, height, framerate_, 0, profile_)) {
      std::cerr << "Cannot open " << video_codec_ << " encoder" << std::endl;
      return false;
    }
    AVCodecContext *encoder_context = encoder_->get_context();
    encoder_out_width_ = encoder_context->width;
    encoder_out_height_ = encoder_context->height;
    encoder_out_pix_fmt_ = encoder_context->pix_fmt;

    // 初始化 FPS 滤镜：当摄像头实际帧率与目标帧率不一致时自动补帧/丢帧
    if (!init_fps_filter(encoder_out_width_, encoder_out_height_, framerate_)) {
      std::cout << "Warning: FPS filter init failed, using raw camera framerate" << std::endl;
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
    // 普通摄像头模式：启动所有线程（4线程流水线）
    decode_thread_ = std::thread(&VideoCapturer::decode_loop, this);
    filter_thread_ = std::thread(&VideoCapturer::filter_loop, this);
    encode_thread_ = std::thread(&VideoCapturer::encode_loop, this);
    send_thread_   = std::thread(&VideoCapturer::send_loop, this);

    // 如果CPU数量大于等于4，则绑定线程到不同的CPU
    int cpu_count = get_cpu_count();
    if (cpu_count >= 4) {
      std::cout << "Detected " << cpu_count << " CPUs, binding threads to different CPUs" << std::endl;
      bind_thread_to_cpu(capture_thread_, 0); // 采集线程绑定到CPU 0
      bind_thread_to_cpu(filter_thread_, 3);  // 滤镜线程绑定到CPU 3
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

  cleanup_fps_filter();

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

void VideoCapturer::set_profile(const std::string &profile) {
  profile_ = profile;
  std::cout << "Video profile set to: " << profile_ << std::endl;
}

bool VideoCapturer::init_fps_filter(int width, int height, int fps) {
  cleanup_fps_filter();

  const AVFilter *buffersrc = avfilter_get_by_name("buffer");
  const AVFilter *buffersink = avfilter_get_by_name("buffersink");
  const AVFilter *fps_filter = avfilter_get_by_name("fps");

  if (!buffersrc || !buffersink || !fps_filter) {
    std::cerr << "Cannot find required filters (buffer/buffersink/fps), "
              << "FFmpeg may be compiled without libavfilter" << std::endl;
    return false;
  }

  fps_filter_graph_ = avfilter_graph_alloc();
  if (!fps_filter_graph_) {
    std::cerr << "Cannot allocate filter graph" << std::endl;
    return false;
  }

  // 变量提前声明，避免 goto 跨越初始化
  char args[256];
  int ret;
  enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
  AVFilterContext *fps_ctx = nullptr;

  // 构建 filtergraph: buffer → fps=N → buffersink
  // 使用微秒级 time_base (1/1000000)，配合真实时间戳 PTS，
  // 确保 fps 滤镜能准确计算帧间隔进行补帧/丢帧
  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=1/1000000:frame_rate=%d",
           width, height, AV_PIX_FMT_YUV420P, fps);

  ret = avfilter_graph_create_filter(&fps_buffer_src_, buffersrc, "in", args, nullptr, fps_filter_graph_);
  if (ret < 0) { goto cleanup; }

  ret = avfilter_graph_create_filter(&fps_buffer_sink_, buffersink, "out", nullptr, nullptr, fps_filter_graph_);
  if (ret < 0) { goto cleanup; }

  // 设置 buffersink 输出格式为 YUV420P
  ret = av_opt_set_int_list(fps_buffer_sink_, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
  if (ret < 0) { goto cleanup; }

  snprintf(args, sizeof(args), "fps=%d", fps);

  ret = avfilter_graph_create_filter(&fps_ctx, fps_filter, "fps", args, nullptr, fps_filter_graph_);
  if (ret < 0) { goto cleanup; }

  // 连接: in → fps → out
  ret = avfilter_link(fps_buffer_src_, 0, fps_ctx, 0);
  if (ret < 0) { goto cleanup; }
  ret = avfilter_link(fps_ctx, 0, fps_buffer_sink_, 0);
  if (ret < 0) { goto cleanup; }

  ret = avfilter_graph_config(fps_filter_graph_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot config filter graph: " << av_error_string(ret) << std::endl;
    goto cleanup;
  }

  target_fps_ = fps;

  std::cout << "FPS filter initialized: " << width << "x" << height
            << " @ " << fps << "fps (vf=fps=" << fps << ")" << std::endl;

  return true;

cleanup:
  std::cerr << "Failed to initialize FPS filter: " << av_error_string(ret) << std::endl;
  cleanup_fps_filter();
  return false;
}

void VideoCapturer::cleanup_fps_filter() {
  if (fps_filter_graph_) {
    avfilter_graph_free(&fps_filter_graph_);
    fps_filter_graph_ = nullptr;
  }
  fps_buffer_src_ = nullptr;
  fps_buffer_sink_ = nullptr;
}


void VideoCapturer::reconfigure(const std::string &resolution, int fps, int bitrate, const std::string &format) {
  // 使用互斥锁保护reconfigure操作，避免竞态条件
  std::lock_guard<std::mutex> lock(config_mutex_);

  std::cout << "Reconfiguring video capturer (stability mode)..." << std::endl;

  // -1/空值表示保持当前值不变
  if (resolution != "-1" && !resolution.empty()) {
    resolution_ = resolution;
    std::cout << "  resolution changed to: " << resolution_ << std::endl;
  }
  if (fps != -1) {
    framerate_ = fps;
    std::cout << "  fps changed to: " << framerate_ << std::endl;
  }
  if (format != "-1" && !format.empty()) {
    video_format_ = format;
    std::cout << "  format changed to: " << video_format_ << std::endl;
  }

  // 如果 fps 和 bitrate 都没变，无需重配置
  if (fps == -1 && bitrate == -1) {
    std::cout << "No encoder parameters changed, skipping reconfiguration" << std::endl;
    return;
  }

  // 稳定性改进：不关闭/重开采集设备，只重配置编码器和FPS滤镜
  // 对网络流模式 (UDP) 也不做任何操作，因为没有本地编码器控制
  if (is_udp_stream_) {
    std::cout << "UDP stream mode, skipping encoder reconfiguration" << std::endl;
    return;
  }

  // 解析当前分辨率获取宽高
  int width = 640, height = 480;
  sscanf(resolution_.c_str(), "%dx%d", &width, &height);
  if (width < height) {
    std::swap(width, height);
  }

  int actual_fps = (fps != -1) ? fps : framerate_;
  int actual_bitrate = (bitrate != -1) ? bitrate : 0;

  std::cout << "Reconfiguring encoder: " << width << "x" << height
            << " " << actual_fps << "fps " << actual_bitrate << "bps" << std::endl;

  // 暂停采集
  pause_capture();

  // 清空队列（帧已被采集但还未处理）
  decode_queue_.clear();
  encode_queue_.clear();
  send_queue_.clear();

  // ===== 关键修复：持有 encoder_mutex_ 保护编码器关闭/重开 =====
  // 防止 encode_loop 线程正在使用 encoder 时被 close_encoder 破坏上下文
  {
    std::lock_guard<std::mutex> lock(encoder_mutex_);

    // 关闭旧编码器
    encoder_->close_encoder();

    // 使用新参数重新打开编码器
    if (!encoder_->open_encoder(width, height, actual_fps, actual_bitrate, profile_)) {
      std::cerr << "Failed to reconfigure encoder" << std::endl;
      resume_capture();
      return;
    }

    // 更新编码器输出参数（SwsContext可能在分辨率不变时不需要重建）
    AVCodecContext *encoder_context = encoder_->get_context();
    if (!encoder_context) {
      std::cerr << "Encoder context is null after reconfiguration" << std::endl;
      resume_capture();
      return;
    }

    bool resolution_changed = (encoder_out_width_ != encoder_context->width ||
                               encoder_out_height_ != encoder_context->height ||
                               encoder_out_pix_fmt_ != encoder_context->pix_fmt);
    encoder_out_width_ = encoder_context->width;
    encoder_out_height_ = encoder_context->height;
    encoder_out_pix_fmt_ = encoder_context->pix_fmt;

    if (resolution_changed && codec_context_) {
      // 只有在分辨率变化时才重建 SwsContext
      if (sws_context_) {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
      }
      sws_context_ = sws_getContext(
          codec_context_->width, codec_context_->height, codec_context_->pix_fmt,
          encoder_out_width_, encoder_out_height_, encoder_out_pix_fmt_,
          SWS_BILINEAR, nullptr, nullptr, nullptr);
      if (!sws_context_) {
        std::cerr << "Cannot recreate SwsContext after reconfigure" << std::endl;
        resume_capture();
        return;
      }
    }
  } // encoder_mutex_ 释放

  // ===== 关键修复：持有 filter_mutex_ 保护 FPS 滤镜重建 =====
  // 防止 filter_loop 线程正在使用 fps_buffer_* 时被清理
  if (fps != -1) {
    std::lock_guard<std::mutex> lock(filter_mutex_);
    if (!init_fps_filter(encoder_out_width_, encoder_out_height_, actual_fps)) {
      std::cout << "Warning: FPS filter re-init failed, using raw framerate" << std::endl;
    }
  }

  // 恢复采集
  resume_capture();

  std::cout << "Video capturer reconfigured successfully (stability mode)" << std::endl;
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

    // 等待 track_callbacks_ 被设置 (多peer支持)
    {
      std::unique_lock<std::mutex> lock(callback_mutex_);
      callback_cv_.wait(lock, [this] { return !track_callbacks_.empty() || !is_running_; });
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

    if (encoder_out_fps <= 0) {
      encoder_out_fps = framerate_;
    }

    // 当 FPS 滤镜激活时，由滤镜负责帧率控制，
    // capture_loop 应尽快送入所有原始帧，不做丢帧和限速
    const bool use_fps_filter = (fps_buffer_src_ != nullptr);
    int frame_drop_factor = 1; // 默认值，可以根据需要调整
    
    // 实际采集 FPS 测量变量
    int actual_capture_fps = 0;
    int fps_calc_counter = 0;
    auto fps_calc_start_time = std::chrono::steady_clock::now();
    int total_captured_frames = 0;

    int frame_counter = 0;
    std::cout << "Encode FPS: " << encoder_out_fps
              << ", Frame Drop Factor: " << frame_drop_factor << std::endl;
    std::cout << "Measuring actual capture FPS..." << std::endl;

    // 等待 track_callbacks_ 被设置 (多peer支持)
    {
      std::unique_lock<std::mutex> lock(callback_mutex_);
      callback_cv_.wait(lock, [this] { return !track_callbacks_.empty() || !is_running_; });
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

        // ========== 帧率限速（Pacing）：控制采集节奏 ==========
        // 当 FPS 滤镜激活时，由滤镜负责帧率转换，
        // capture_loop 尽快读取所有原始帧送入解码队列
        if (!use_fps_filter) {
          // 使用静态变量记录下一帧的期望时间（更精确的 pacing）
          static auto next_frame_time = std::chrono::steady_clock::now();
          
          // 使用实际测量的 FPS，如果还没测量到则使用编码器输出 FPS 作为参考
          int current_capture_fps = (actual_capture_fps > 0) ? actual_capture_fps : encoder_out_fps;
          if (current_capture_fps <= 0) {
            current_capture_fps = 30;  // 默认 30 FPS
          }
          
          int64_t frame_interval_us = 1000000 / current_capture_fps;  // 微秒/帧
          
          auto now = std::chrono::steady_clock::now();
          
          // 如果还没到下一帧时间，等待
          if (now < next_frame_time) {
            std::this_thread::sleep_until(next_frame_time);
          }
          
          // 更新下一帧时间（固定间隔，不考虑实际采集耗时）
          next_frame_time += std::chrono::microseconds(frame_interval_us);
          
          // 如果落后太多（超过3帧），跳帧以避免累积延迟
          auto now2 = std::chrono::steady_clock::now();
          if (now2 > next_frame_time + std::chrono::microseconds(frame_interval_us * 3)) {
            if (debug_enabled_) {
              std::cout << "Pacing: skipping frames due to backlog" << std::endl;
            }
            next_frame_time = now2;
          }
        }

      int ret = av_read_frame(format_context_, packet);
      if (ret < 0) {
        if (ret != AVERROR(EAGAIN)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        continue;
      }

      if (packet->stream_index == video_stream_index_) {
        // 实际采集 FPS 测量（优化：只在需要时才获取时间戳）
        total_captured_frames++;
        
        // 每秒计算一次实际采集 FPS（避免每帧都调用 steady_clock::now()）
        if ((total_captured_frames & 31) == 0) {  // 每 32 帧检查一次（约 0.25 秒）
          auto now = std::chrono::steady_clock::now();
          auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              now - fps_calc_start_time).count();
          
          if (elapsed_ms >= 1000) {
            actual_capture_fps = static_cast<int>(
                total_captured_frames * 1000 / elapsed_ms);
            
            // 根据实际采集 FPS 动态调整 frame_drop_factor
            if (!use_fps_filter && encoder_out_fps > 0 && actual_capture_fps > 0) {
              if (encoder_out_fps < actual_capture_fps) {
                frame_drop_factor = std::max(1, static_cast<int>(
                    std::round(static_cast<double>(actual_capture_fps) /
                               static_cast<double>(encoder_out_fps))));
              } else {
                frame_drop_factor = 1;
              }
            }
            
            std::cout << "Actual Capture FPS: " << actual_capture_fps
                      << ", Encode FPS: " << encoder_out_fps
                      << ", Frame Drop Factor: " << frame_drop_factor
                      << ", Total Captured: " << total_captured_frames
                      << std::endl;
            
            // 重置计数器
            fps_calc_start_time = now;
            total_captured_frames = 0;
          }
        }

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

      // 解码帧直接推入 filter_queue，由 filter_loop 线程处理
      AVFrame *ref_frame = av_frame_alloc();
      av_frame_ref(ref_frame, frame);

      if (!filter_queue_.try_push(ref_frame)) {
        if (debug_enabled_) {
          std::cout << "Filter queue full, dropping decoded frame" << std::endl;
        }
        av_frame_free(&ref_frame);
      }
    }

    av_packet_free(&packet);
  }

  av_frame_free(&frame);
  std::cout << "Video Decode thread exiting" << std::endl;
}

void VideoCapturer::filter_loop() {
  AVFrame *frame = nullptr;

  while (is_running_) {
    filter_queue_.wait_pop(frame);

    if (!frame) {
      if (!is_running_) break;
      continue;
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

    // 持锁检查并访问 FPS 滤镜，防止 reconfigure 在检查后
    // 调用 cleanup_fps_filter 释放正在使用的滤镜
    bool use_fps_filter;
    {
      std::lock_guard<std::mutex> lock(filter_mutex_);
      use_fps_filter = (fps_buffer_src_ && fps_buffer_sink_);
    }

    if (use_fps_filter) {
      // 使用 FPS 滤镜：补帧（摄像头fps < 目标）或丢帧（摄像头fps > 目标）
      //  用真实时间戳(微秒)作为 PTS，配合 filter time_base=1/1000000
      frame->pts = av_gettime_relative();

      std::lock_guard<std::mutex> lock(filter_mutex_);
      int ret = av_buffersrc_add_frame_flags(fps_buffer_src_, frame,
                                             AV_BUFFERSRC_FLAG_KEEP_REF);
      av_frame_free(&frame);  // 滤镜持有引用，释放原始帧

      if (ret < 0) {
        if (debug_enabled_) {
          std::cerr << "Error feeding frame to FPS filter: "
                    << av_error_string(ret) << std::endl;
        }
        continue;
      }

      // 从滤镜输出端取出所有可用帧
      while (is_running_) {
        AVFrame *filtered_frame = av_frame_alloc();

        ret = av_buffersink_get_frame(fps_buffer_sink_, filtered_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          av_frame_free(&filtered_frame);
          break;
        } else if (ret < 0) {
          av_frame_free(&filtered_frame);
          break;
        }

        // 重置 PTS：FPS filter 输出微秒级 PTS，编码器 time_base 是帧为单位，
        // 让编码器自己的帧计数器来分配 PTS，避免 GOP 被破坏
        filtered_frame->pts = AV_NOPTS_VALUE;

        if (!encode_queue_.try_push(filtered_frame)) {
          if (debug_enabled_) {
            std::cout << "Video Encode queue full, dropping filtered frame"
                      << std::endl;
            std::cout << "Video Encode queue Len: " << encode_queue_.size()
                      << ", Capacity: " << encode_queue_.capacity() << std::endl;
          }
          av_frame_free(&filtered_frame);
          encode_queue_.clear();
          send_queue_.clear();
        }
      }
    } else {
      // 无 FPS 滤镜时：直接推入编码队列
      frame->pts = AV_NOPTS_VALUE;
      if (!encode_queue_.try_push(frame)) {
        if (debug_enabled_) {
          std::cout << "Video Encode queue full, dropping frame" << std::endl;
        }
        av_frame_free(&frame);
        encode_queue_.clear();
        send_queue_.clear();
      }
    }
  }

  std::cout << "Video Filter thread exiting" << std::endl;
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

    // 持锁保护 encode 操作，确保 reconfigure 的 close/open 不会并发破坏编码器上下文
    bool encoded;
    {
      std::lock_guard<std::mutex> lock(encoder_mutex_);
      encoded = encoder_->encode_frame(frame, packet);
    }
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
  // 发送节流：控制发送节奏，避免网络突发
  static auto last_send_time = std::chrono::steady_clock::now();
  static int64_t bytes_sent_in_window = 0;
  static constexpr int64_t kMaxBytesPerWindow = 150000; // 每 100ms 窗口最大发送量 (约 12Mbps)
  static constexpr int64_t kWindowDurationUs = 100000;   // 窗口时长 100ms

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

    // ========== 发送节流（Pacing）：平滑网络输出 ==========
    auto now = std::chrono::steady_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_send_time).count();

    // 检查是否需要重置窗口或等待
    if (elapsed_us >= kWindowDurationUs) {
      // 新窗口开始，重置计数器
      last_send_time = now;
      bytes_sent_in_window = 0;
    } else if (bytes_sent_in_window + packet->size > kMaxBytesPerWindow && !is_fake_camera_) {
      // 本窗口剩余空间不足（仅对真实摄像头生效，fake camera 已有 capture pacing）
      int64_t wait_time = kWindowDurationUs - elapsed_us;
      if (wait_time > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(wait_time));
        // 重置窗口
        last_send_time = std::chrono::steady_clock::now();
        bytes_sent_in_window = 0;
      }
    }

    bytes_sent_in_window += packet->size;

    // Send data to all registered callbacks (multiple peer support)
    auto data = reinterpret_cast<const std::byte *>(packet->data);
    size_t data_size = packet->size;

    // 使用互斥锁保护callbacks map的访问
    std::lock_guard<std::mutex> lock(callbacks_mutex_);

    for (const auto &pair : track_callbacks_) {
      if (pair.second) {
        pair.second(data, data_size);
      }
    }

    if (debug_enabled_ && !track_callbacks_.empty()) {
      std::cout << "Video sent: size=" << packet->size
                << ", Callbacks: " << track_callbacks_.size() << std::endl;
    } else if (debug_enabled_) {
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
  // 标记为池帧，用于 release 时区分滤镜输出的非池帧
  frame->opaque = this;
  return frame;
}

void VideoCapturer::release_scaled_frame(AVFrame *frame) {
  if (!frame) {
    return;
  }
  // 只回收池帧（opaque 标记），非池帧（如 fps 滤镜输出）直接释放
  if (frame->opaque == this) {
    frame->opaque = nullptr;  // 清除标记
    std::lock_guard<std::mutex> lock(frame_pool_mutex_);
    constexpr size_t kMaxPoolSize = 32;
    if (scaled_frame_pool_.size() < kMaxPoolSize) {
      scaled_frame_pool_.push_back(frame);
      return;  // 已回池，不 free
    }
  }
  av_frame_free(&frame);
}

void VideoCapturer::clear_frame_pool() {
  std::lock_guard<std::mutex> lock(frame_pool_mutex_);
  for (auto *frame : scaled_frame_pool_) {
    av_frame_free(&frame);
  }
  scaled_frame_pool_.clear();
}