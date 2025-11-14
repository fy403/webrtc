#include "audio_player.h"
#include "debug_utils.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

AudioPlayer::AudioPlayer(const std::string &audio_device,
                         const AudioDeviceParams &params)
    : audio_device_(audio_device), params_(params),
      opus_decoder_(std::make_unique<OpusDecoder>()),
      decode_queue_(1000),        // 减少队列大小避免积压
      audio_sample_queue_(2000) { // 适当增加音频样本队列

  decode_queue_.set_deleter([](AVPacket *&packet) {
    if (packet) {
      av_packet_free(&packet);
    }
  });

  audio_sample_queue_.set_deleter([](AVFrame *&frame) {
    if (frame) {
      av_frame_free(&frame);
    }
  });
}

AudioPlayer::~AudioPlayer() { stop(); }

void AudioPlayer::start() {
  if (running_)
    return;

  std::cout << "Starting audio player..." << std::endl;

  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
    return;
  }

  // 初始化 Opus 解码器 - 不指定具体参数，让解码器自动识别
  if (!opus_decoder_->open_decoder()) {
    std::cerr << "Failed to initialize Opus decoder" << std::endl;
    SDL_Quit();
    return;
  }

  if (!initSDLAudio()) {
    std::cerr << "Failed to initialize SDL audio" << std::endl;
    cleanup();
    SDL_Quit();
    return;
  }

  running_ = true;

  // 启动解码线程
  decode_thread_ = std::thread(&AudioPlayer::decodeThread, this);

  std::cout << "Audio player started successfully" << std::endl;
}

void AudioPlayer::stop() {
  if (!running_)
    return;

  std::cout << "Stopping audio player..." << std::endl;
  running_ = false;

  // 清空队列唤醒等待的线程
  decode_queue_.clear();
  audio_sample_queue_.clear();

  if (decode_thread_.joinable()) {
    decode_thread_.join();
  }

  cleanup();
  SDL_Quit();

  // 输出最终统计
  std::cout << "Audio player stopped. Final stats - "
            << "Packets received: " << packets_received_.load()
            << ", Processed: " << packets_processed_.load()
            << ", Underruns: " << audio_underruns_.load() << std::endl;
}

bool AudioPlayer::initSDLAudio() {
  SDL_AudioSpec desired, obtained;
  SDL_zero(desired);

  // 先使用默认参数，后续再根据实际解码结果调整
  desired.freq = params_.sample_rate;
  desired.format = AUDIO_S16;
  desired.channels = params_.channels;
  desired.samples = 1024; // 优化缓冲区大小
  desired.callback = &AudioPlayer::sdlAudioCallback;
  desired.userdata = this;

  // 打印所有可用的音频设备名称
  std::cout << "Available audio devices:" << std::endl;
  for (int i = 0; i < SDL_GetNumAudioDevices(0); ++i) {
    std::cout << "  [" << i << "] " << SDL_GetAudioDeviceName(i, 0)
              << std::endl;
  }

  if (!audio_device_.empty())
    std::cout << "Speaker device name: " << audio_device_ << std::endl;
  else
    std::cout << "Using default audio device" << std::endl;

  const char *device_name =
      audio_device_.empty() ? nullptr : audio_device_.c_str();
  audio_device_id_ =
      SDL_OpenAudioDevice(device_name, 0, &desired, &obtained, 0);

  if (audio_device_id_ == 0) {
    std::cerr << "Failed to open audio device(" << device_name
              << "): " << SDL_GetError() << std::endl;
    return false;
  }

  audio_spec_ = obtained;

  std::cout << "SDL audio initialized: " << obtained.freq << "Hz, "
            << static_cast<int>(obtained.channels) << " channels, "
            << obtained.samples << " samples" << std::endl;

  // 启动SDL音频播放
  SDL_PauseAudioDevice(audio_device_id_, 0);

  std::cout << "SDL Audio Device: "
            << SDL_GetAudioDeviceName(audio_device_id_, 0)
            << " (ID: " << audio_device_id_ << ")" << std::endl;

  return true;
}

bool AudioPlayer::initResampler() { return true; }

void AudioPlayer::cleanup() {
  if (audio_device_id_ != 0) {
    SDL_PauseAudioDevice(audio_device_id_, 1);
    SDL_CloseAudioDevice(audio_device_id_);
    audio_device_id_ = 0;
  }

  if (opus_decoder_) {
    opus_decoder_->close_decoder();
  }

  // 清理重采样器
  if (swr_ctx_) {
    swr_free(&swr_ctx_);
    swr_ctx_ = nullptr;
  }
}

void AudioPlayer::receiveAudioData(const rtc::binary &data,
                                   const rtc::FrameInfo &info) {

  uint64_t timestamp_us = info.timestamp;
  uint8_t payloadType = info.payloadType;
  packets_received_++;

  // 性能统计：每2秒输出一次
  static auto last_stat_time = std::chrono::steady_clock::now();
  static int local_packet_count = 0;

  local_packet_count++;
  auto now = std::chrono::steady_clock::now();
  //  if (now - last_stat_time > std::chrono::seconds(2)) {
  //    std::cout << "Audio receive rate: " << local_packet_count / 2.0
  //              << " packets/s" << std::endl;
  //    local_packet_count = 0;
  //    last_stat_time = now;
  //    std::cout << "Audio RTP type: " << int(payloadType) << std::endl;
  //  }

  AVPacket *packet = av_packet_alloc();
  if (packet) {
    if (av_new_packet(packet, static_cast<int>(data.size())) == 0) {
      std::memcpy(packet->data, data.data(), data.size());
    } else {
      av_packet_free(&packet);
      return;
    }

    packet->pts = timestamp_us;
    packet->dts = timestamp_us;

    if (!decode_queue_.try_push(packet)) {
      av_packet_free(&packet);
    }
  }
}

void AudioPlayer::sdlAudioCallback(void *userdata, Uint8 *stream, int len) {
  auto player = static_cast<AudioPlayer *>(userdata);
  player->audioCallback(stream, len);
}

void AudioPlayer::audioCallback(Uint8 *stream, int len) {
  SDL_memset(stream, 0, len);

  int16_t *output_buffer = reinterpret_cast<int16_t *>(stream);
  int samples_needed = len / sizeof(int16_t);
  int samples_written = 0;

  // 音量控制参数 (0-100%)
  static const float volume_scale = 0.1f; // 降低音量以减少失真

  while (samples_written < samples_needed) {
    AVFrame *frame = nullptr;
    if (!audio_sample_queue_.pop(frame)) {
      // 队列为空，跳出循环
      break;
    }

    if (!frame || !frame->data[0] || frame->nb_samples <= 0) {
      if (frame)
        av_frame_free(&frame);
      continue;
    }

    // 验证音频格式（应该是S16）
    if (frame->format != AV_SAMPLE_FMT_S16) {
      std::cerr << "Unexpected audio format in callback: " << frame->format
                << std::endl;
      av_frame_free(&frame);
      continue;
    }

    int16_t *frame_data = reinterpret_cast<int16_t *>(frame->data[0]);
    int samples_available = frame->nb_samples * frame->channels;
    int samples_to_copy =
        std::min(samples_available, samples_needed - samples_written);

    // 拷贝并应用音量控制的音频数据
    // 使用更平滑的音量控制算法以减少高频失真
    for (int i = 0; i < samples_to_copy; ++i) {
      // 使用浮点运算进行更精确的音量控制
      float scaled_sample = static_cast<float>(frame_data[i]) * volume_scale;
      // 确保值在16位范围内
      output_buffer[samples_written + i] = static_cast<int16_t>(
          std::max(-32768.0f, std::min(32767.0f, scaled_sample)));
    }
    samples_written += samples_to_copy;

    // 处理未用完的数据
    if (samples_to_copy < samples_available) {
      int remaining_samples = samples_available - samples_to_copy;
      AVFrame *remaining_frame = av_frame_alloc();

      if (remaining_frame) {
        // 复制frame属性
        if (av_frame_ref(remaining_frame, frame) == 0) {
          // 设置剩余数据的指针
          remaining_frame->data[0] =
              reinterpret_cast<uint8_t *>(frame_data + samples_to_copy);
          remaining_frame->nb_samples = remaining_samples / frame->channels;

          // 尝试放回队列
          if (!audio_sample_queue_.try_push(remaining_frame)) {
            av_frame_free(&remaining_frame);
          }
        } else {
          av_frame_free(&remaining_frame);
        }
      }
    }

    av_frame_free(&frame);
  }

  // 处理音频欠载
  if (samples_written < samples_needed) {
    audio_underruns_++;

    // 用静音填充剩余缓冲区
    memset(output_buffer + samples_written, 0,
           (samples_needed - samples_written) * sizeof(int16_t));

    // 每50次欠载输出一次警告
    if (audio_underruns_.load() % 50 == 0) {
      std::cout << "Audio underrun #" << audio_underruns_.load()
                << ": requested " << samples_needed << " samples, got "
                << samples_written << std::endl;
    }
  }
}

void AudioPlayer::decodeThread() {
  std::cout << "Audio decode thread started" << std::endl;

  AVFrame *decoded_frame = av_frame_alloc();
  AVFrame *resampled_frame = av_frame_alloc();

  if (!decoded_frame || !resampled_frame) {
    std::cerr << "Failed to allocate frames" << std::endl;
    if (decoded_frame)
      av_frame_free(&decoded_frame);
    if (resampled_frame)
      av_frame_free(&resampled_frame);
    return;
  }

  auto last_log_time = std::chrono::steady_clock::now();
  auto last_perf_time = last_log_time;
  int packets_in_period = 0;

  // 只在调试时保存音频文件
  const bool enable_debug_save = true; // 改为true进行调试

  // 标记是否已经初始化了重采样器
  bool resampler_initialized = false;

  while (running_) {
    AVPacket *packet = nullptr;

    // 使用带超时的等待，避免线程无法及时退出
    decode_queue_.wait_and_pop(packet);

    if (!running_) {
      if (packet)
        av_packet_free(&packet);
      break;
    }

    packets_processed_++;
    packets_in_period++;

    if (packet->size > 0 &&
        opus_decoder_->decode_packet(packet, decoded_frame)) {

      // 第一次成功解码后，获取实际参数并初始化重采样器
      if (!resampler_initialized) {
        // 获取实际解码参数
        int actual_sample_rate = opus_decoder_->get_sample_rate();
        int actual_channels = opus_decoder_->get_channels();
        AVSampleFormat actual_sample_fmt = opus_decoder_->get_sample_fmt();

        // 定义输出参数为局部变量
        int out_sample_rate = params_.sample_rate;
        int out_channels = params_.channels;
        AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
        uint64_t out_channel_layout =
            av_get_default_channel_layout(out_channels);
        uint64_t in_channel_layout =
            av_get_default_channel_layout(actual_channels);

        // 配置重采样器：从实际格式转换为S16（有符号16位整数交错）
        swr_ctx_ = swr_alloc_set_opts(nullptr,
                                      // 输出格式 - 使用配置参数
                                      out_channel_layout, out_sample_fmt,
                                      out_sample_rate,
                                      // 输入格式（Opus解码器输出）
                                      in_channel_layout, actual_sample_fmt,
                                      actual_sample_rate, 0, nullptr);

        if (!swr_ctx_) {
          std::cerr << "Failed to allocate resampler context" << std::endl;
        } else {
          // 设置重采样器选项以提高质量
          av_opt_set_double(swr_ctx_, "cutoff", 0.98, 0);
          av_opt_set(swr_ctx_, "filter_type", "kaiser", 0);
          av_opt_set_double(swr_ctx_, "kaiser_beta", 9.0, 0);

          int ret = swr_init(swr_ctx_);
          if (ret < 0) {
            std::cerr << "Failed to initialize resampler: " << ret << std::endl;
            swr_free(&swr_ctx_);
            swr_ctx_ = nullptr;
          } else {
            resampler_initialized = true;
            std::cout << "Resampler initialized successfully" << std::endl;
            std::cout << "Out sample rate: " << out_sample_rate << std::endl;
            std::cout << "Out sample format: "
                      << av_get_sample_fmt_name(out_sample_fmt) << std::endl;
            std::cout << "Out channels: " << out_channels << std::endl;
          }
        }
      }

      if (resampler_initialized) {
        // 重采样：实际格式 -> S16
        resampled_frame->channel_layout =
            av_get_default_channel_layout(params_.channels);
        resampled_frame->sample_rate = params_.sample_rate;
        resampled_frame->format = AV_SAMPLE_FMT_S16;

        int ret = swr_convert_frame(swr_ctx_, resampled_frame, decoded_frame);
        if (ret == AVERROR_INPUT_CHANGED) {
          std::cerr << "Audio resampling context needs reinitialization due to "
                       "input change"
                    << std::endl;
          // 重新初始化重采样器
          swr_free(&swr_ctx_);
          swr_ctx_ = nullptr;

          // 获取新的实际解码参数
          int actual_sample_rate = opus_decoder_->get_sample_rate();
          int actual_channels = opus_decoder_->get_channels();
          AVSampleFormat actual_sample_fmt = opus_decoder_->get_sample_fmt();

          swr_ctx_ = swr_alloc_set_opts(
              nullptr,
              // 输出格式
              av_get_default_channel_layout(params_.channels),
              AV_SAMPLE_FMT_S16, params_.sample_rate,
              // 输入格式（Opus解码器输出）
              av_get_default_channel_layout(actual_channels), actual_sample_fmt,
              actual_sample_rate, 0, nullptr);

          if (!swr_ctx_) {
            std::cerr << "Failed to allocate resampler context" << std::endl;
          } else {
            // 设置重采样器选项以提高质量
            av_opt_set_double(swr_ctx_, "cutoff", 0.98, 0);
            av_opt_set(swr_ctx_, "filter_type", "kaiser", 0);
            av_opt_set_double(swr_ctx_, "kaiser_beta", 9.0, 0);

            int init_ret = swr_init(swr_ctx_);
            if (init_ret < 0) {
              std::cerr << "Failed to reinitialize resampler: " << init_ret
                        << std::endl;
              swr_free(&swr_ctx_);
              swr_ctx_ = nullptr;
            } else {
              // 重新尝试转换
              ret = swr_convert_frame(swr_ctx_, resampled_frame, decoded_frame);
              if (ret < 0) {
                std::cerr << "Failed to resample audio frame after reinit: "
                          << ret << std::endl;
              }
            }
          }
        } else if (ret < 0) {
          std::cerr << "Failed to resample audio frame: " << ret << std::endl;
        } else {
          // 将重采样后的帧放入队列
          AVFrame *queue_frame = av_frame_alloc();
          if (queue_frame) {
            av_frame_move_ref(queue_frame, resampled_frame);

            // 使用非阻塞推送
            if (!audio_sample_queue_.try_push(queue_frame)) {
              // 队列满时丢弃帧
              av_frame_free(&queue_frame);
            }
          }
        }
      } else {
        std::cerr << "Resampler not initialized, skipping frame" << std::endl;
      }
    }

    av_packet_free(&packet);

    // 性能统计和日志输出
    //    auto now = std::chrono::steady_clock::now();
    //    if (now - last_perf_time > std::chrono::seconds(5)) {
    //      std::cout << "Audio decode: " << packets_in_period / 5.0 << "
    //      packets/s, "
    //                << "queue sizes: decode=" << decode_queue_.size()
    //                << ", audio=" << audio_sample_queue_.size() << std::endl;
    //      packets_in_period = 0;
    //      last_perf_time = now;
    //    }
  }

  av_frame_free(&decoded_frame);
  av_frame_free(&resampled_frame);

  if (enable_debug_save) {
    DebugUtils::finalize_raw_audio_frame_file();
  }

  std::cout << "Audio decode thread stopped" << std::endl;
}