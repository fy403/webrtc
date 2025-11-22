#include "video_capturer.h"
#include "debug_utils.h"
#include "encoder.h"
#include "h264_encoder.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <random>
#include <sstream>
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
                             size_t encode_queue_capacity,
                             size_t send_queue_capacity)
    : Capture(debug_enabled, encode_queue_capacity, send_queue_capacity),
      device_(device), resolution_(resolution), framerate_(framerate),
      video_format_(video_format) {
  avdevice_register_all();
  encoder_ = std::make_unique<H264Encoder>(debug_enabled);
}

VideoCapturer::~VideoCapturer() { stop(); }

// Inherited from Capture base class
// void VideoCapturer::set_track_callback(TrackCallback callback)

bool VideoCapturer::start() {
  AVInputFormat *input_format = av_find_input_format("v4l2");
  if (!input_format) {
    std::cerr << "Cannot find V4L2 input input_format" << std::endl;
    return false;
  }
  std::string device_path = device_;

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
  int ret = avformat_open_input(&format_context_, device_path.c_str(),
                                input_format, &options);
  if (ret < 0) {
    std::cerr << "Cannot open video device: " << av_error_string(ret)
              << std::endl;
    return false;
  }

  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot find stream info: " << av_error_string(ret)
              << std::endl;
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

  // Initialize encoder
  if (!encoder_->open_encoder(width, height, framerate_)) {
    std::cerr << "Cannot open H.264 encoder" << std::endl;
    return false;
  }
  AVCodecContext *encoder_context = encoder_->get_context();
  sws_context_ = sws_getContext(
      codec_context_->width, codec_context_->height, codec_context_->pix_fmt,
      encoder_context->width, encoder_context->height, encoder_context->pix_fmt,
      SWS_BILINEAR, nullptr, nullptr, nullptr);

  if (!sws_context_) {
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

void VideoCapturer::stop() {
  Capture::stop();

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
}

void VideoCapturer::capture_loop() {
  AVPacket *packet = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();

  int encoder_out_fps = encoder_->get_context()->framerate.num;
  int capture_in_fps =
      format_context_->streams[video_stream_index_]->avg_frame_rate.num;
  static int saved_count = 0;
  static int frame_counter = 0;
  int frame_drop_factor = 1; // 默认值，可以根据需要调整
  if (encoder_out_fps < capture_in_fps) {
    frame_drop_factor = std::max(1, capture_in_fps / encoder_out_fps);
  }
  std::cout << "Capture FPS: " << capture_in_fps
            << ", Encode FPS: " << encoder_out_fps
            << ", Frame Drop Factor: " << frame_drop_factor << std::endl;

  // 等待 track_callback_ 被设置
  {
    std::unique_lock<std::mutex> lock(callback_mutex_);
    callback_cv_.wait(lock, [this] { return track_callback_ || !is_running_; });
  }

  if (!is_running_) {
    av_frame_free(&frame);
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
        // 跳过这一帧（不进行编码）
        av_packet_unref(packet);
        continue;
      }

      ret = avcodec_send_packet(codec_context_, packet);
      if (ret < 0) {
        av_packet_unref(packet);
        continue;
      }

      while (ret >= 0) {
        ret = avcodec_receive_frame(codec_context_, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          break;
        } else if (ret < 0) {
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

        // Convert frame input_format and put in encode queue
        AVFrame *scaled_frame = av_frame_alloc();
        scaled_frame->format = AV_PIX_FMT_YUV420P;
        scaled_frame->width = encoder_->get_context()->width;
        scaled_frame->height = encoder_->get_context()->height;

        av_frame_get_buffer(scaled_frame, 0);

        sws_scale(sws_context_, frame->data, frame->linesize, 0, frame->height,
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

void VideoCapturer::encode_loop() {
  AVPacket *packet = av_packet_alloc();

  while (is_running_) {
    AVFrame *frame = nullptr;

    encode_queue_.wait_and_pop(frame);

    if (!frame)
      continue;

    bool encoded = encoder_->encode_frame(frame, packet);
    av_frame_free(&frame);

    if (encoded) {
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

void VideoCapturer::send_loop() {
  while (is_running_) {
    AVPacket *packet = nullptr;

    // Get packet from send queue
    send_queue_.wait_and_pop(packet);

    if (!packet)
      continue;

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
}