#include "audio_capturer.h"
#include "debug_utils.h"
#include "encoder.h"
#include "h264_encoder.h"
#include "opus_encoder.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <random>
#include <sstream>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
}

#include "rtc/rtc.hpp"

// 错误处理函数替代 av_err2str
extern std::string av_error_string(int errnum);

AudioCapturer::AudioCapturer(const AudioDeviceParams &params,
                             bool debug_enabled, size_t encode_queue_capacity,
                             size_t send_queue_capacity)
    : Capture(debug_enabled, encode_queue_capacity, send_queue_capacity),
      audio_params_(params) {
  avdevice_register_all();
  // Initialize Opus encoder instead of AAC
  encoder_ = std::make_unique<OpusEncoder>(debug_enabled);
}

AudioCapturer::~AudioCapturer() { stop(); }

bool AudioCapturer::start() {
  if (!audio_params_.is_valid()) {
    std::cerr << "Invalid audio device parameters" << std::endl;
    return false;
  }

  AVInputFormat *input_format =
      av_find_input_format(audio_params_.input_format.c_str());
  if (!input_format) {
    std::cerr << "Cannot find ALSA input input_format" << std::endl;
    return false;
  }

  // 构建输入设备路径
  std::string device_path = audio_params_.device;

  AVDictionary *options = nullptr;
  av_dict_set(&options, "channels",
              std::to_string(audio_params_.channels).c_str(), 0);
  av_dict_set(&options, "sample_rate",
              std::to_string(audio_params_.sample_rate).c_str(), 0);
  // 优化 ALSA 参数以减少 xrun
  av_dict_set(&options, "thread_queue_size", "8192", 0); // 增大线程队列

  // 关键：调整缓冲区参数
  av_dict_set(&options, "buffer_size", "16384", 0); // 增大缓冲区大小
  av_dict_set(&options, "period_size", "1024", 0);  // 减小周期大小

  int ret = avformat_open_input(&format_context_, device_path.c_str(),
                                input_format, &options);
  if (ret < 0) {
    std::cerr << "Cannot open audio device: " << av_error_string(ret)
              << std::endl;
    return false;
  }

  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot find stream info: " << av_error_string(ret)
              << std::endl;
    return false;
  }

  audio_stream_index_ = -1;
  for (unsigned int i = 0; i < format_context_->nb_streams; i++) {
    if (format_context_->streams[i]->codecpar->codec_type ==
        AVMEDIA_TYPE_AUDIO) {
      audio_stream_index_ = i;
      break;
    }
  }

  if (audio_stream_index_ == -1) {
    std::cerr << "Cannot find audio stream" << std::endl;
    return false;
  }

  AVCodecParameters *codec_params =
      format_context_->streams[audio_stream_index_]->codecpar;
  const AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
  if (!codec) {
    std::cerr << "Cannot find decoder for audio stream" << std::endl;
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
  OpusEncoder *opus_encoder = dynamic_cast<OpusEncoder *>(encoder_.get());
  if (opus_encoder) {
    if (!opus_encoder->open_encoder(codec_context_->sample_rate,
                                    codec_context_->channels,
                                    codec_context_->sample_fmt)) {
      std::cerr << "Cannot open Opus encoder" << std::endl;
      return false;
    }
  }

  is_running_ = true;
  // 等待 track_callback_ 设置后再启动采集线程
  capture_thread_ = std::thread(&AudioCapturer::capture_loop, this);
  encode_thread_ = std::thread(&AudioCapturer::encode_loop, this);
  send_thread_ = std::thread(&AudioCapturer::send_loop, this);

  std::cout << "Audio capture started successfully" << std::endl;
  return true;
}

void AudioCapturer::stop() {
  Capture::stop();

  if (codec_context_) {
    avcodec_free_context(&codec_context_);
    codec_context_ = nullptr;
  }

  if (format_context_) {
    avformat_close_input(&format_context_);
    format_context_ = nullptr;
  }
}

void AudioCapturer::capture_loop() {
  AVPacket *packet = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  std::string wav_filename = "captured_audio.wav";
  // 等待 track_callback_ 被设置
  {
    std::unique_lock<std::mutex> lock(callback_mutex_);
    callback_cv_.wait(
        lock, [this] { return track_callback_ != nullptr || !is_running_; });
  }

  if (!is_running_) {
    av_frame_free(&frame);
    av_packet_free(&packet);
    return;
  }
  static auto start_time = std::chrono::steady_clock::now();
  std::cout << "Capture thread started" << std::endl;
  while (is_running_) {
    if (is_paused_) {
      std::unique_lock<std::mutex> lock(callback_mutex_);
      callback_cv_.wait(lock, [this] { return !is_paused_ || !is_running_; });
      continue;
    }
    // 读取数据包
    int ret = av_read_frame(format_context_, packet);
    if (ret < 0) {
      if (ret == AVERROR(EAGAIN)) {
        std::this_thread::sleep_for(
            std::chrono::microseconds(50)); // 减少等待时间
      } else {
        // 检查是否是 xrun 错误
        if (ret == AVERROR(EIO) || ret == AVERROR_EXIT) {
          if (debug_enabled_) {
            std::cerr
                << "ALSA xrun detected , consider adjusting buffer parameters"
                << std::endl;
          }
        } else {
          std::cerr << "av_read_frame error: " << av_error_string(ret)
                    << std::endl;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1)); // 更短的恢复等待
      }
      continue;
    }

    if (packet->stream_index == audio_stream_index_) {
      ret = avcodec_send_packet(codec_context_, packet);
      if (ret < 0) {
        std::cerr << "avcodec_send_packet error: " << av_error_string(ret)
                  << std::endl;
        av_packet_unref(packet);
        continue;
      }

      while (ret >= 0) {
        // 解码数据包
        ret = avcodec_receive_frame(codec_context_, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          break;
        } else if (ret < 0) {
          std::cerr << "avcodec_receive_frame error: " << av_error_string(ret)
                    << std::endl;
          break;
        }
        if (debug_enabled_) {
          auto current_time = std::chrono::steady_clock::now();
          if (current_time - start_time <= std::chrono::seconds(10)) {
            DebugUtils::save_raw_audio_packet(packet, wav_filename);
          } else {
            DebugUtils::finalize_raw_audio_file();
          }
        }

        // 使用非阻塞方式推入队列
        if (!encode_queue_.try_push(frame)) {
          if (debug_enabled_) {
            std::cout << "Encode queue full, dropping audio frame" << std::endl;
            std::cout << "Encode queue Len: " << encode_queue_.size()
                      << ", Capacity: " << encode_queue_.capacity()
                      << std::endl;
          }
          av_frame_unref(frame);
        } else {
          frame = av_frame_alloc();
        }
      }
    }
    av_packet_unref(packet);
  }

  av_frame_free(&frame);
  av_packet_free(&packet);
}
void AudioCapturer::encode_loop() {
  AVPacket *packet = av_packet_alloc();
  AVFrame *frame = nullptr;

  while (is_running_) {
    encode_queue_.wait_and_pop(frame);
    if (!frame)
      continue;

    if (!is_running_) {
      av_packet_free(&packet);
      break;
    }

    bool encoded = encoder_->encode_frame(frame, packet);
    if (encoded) {
      send_queue_.push(packet);
      packet = av_packet_alloc();
    }
    av_frame_free(&frame);
  }
  av_packet_free(&packet);
}

void AudioCapturer::send_loop() {
  AVPacket *packet = nullptr;
  while (is_running_) {
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
      if (debug_enabled_) {
        std::cout << "Send encoded packet: size=" << packet->size
                  << ", Send queue Len: " << send_queue_.size() << std::endl;
      }
    } else {
      std::cout << "Drop packet! No callback set." << std::endl;
    }
    av_packet_free(&packet);
  }
}