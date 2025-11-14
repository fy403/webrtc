#include "audio_player.h"
#include "debug_utils.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

AudioPlayer::AudioPlayer(const AudioPlayerDeviceParams &params)
    : params_(params), opus_decoder_(std::make_unique<OpusDecoder>()),
      decode_queue_(50),         // 减少队列大小以降低延迟
      audio_sample_queue_(100) { // 减少队列大小以降低延迟

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
  setVolume(params_.volume);
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
  //  if (!opus_decoder_->open_decoder()) {
  //    std::cerr << "Failed to initialize Opus decoder" << std::endl;
  //    SDL_Quit();
  //    return;
  //  }

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

bool AudioPlayer::initSDLAudio(int sample_rate, int channels,
                               SDL_AudioFormat format, Uint16 frame_size) {
  SDL_AudioSpec desired, obtained;
  SDL_zero(desired);

  // 根据实际音频参数配置SDL
  desired.freq = sample_rate;
  desired.format = format;
  desired.channels = channels;
  desired.samples = frame_size; // 使用OPUS标准帧大小以减少延迟
  desired.callback = &AudioPlayer::sdlAudioCallback;
  desired.userdata = this;

  // 打印所有可用的音频设备名称
  std::cout << "Available audio devices:" << std::endl;
  for (int i = 0; i < SDL_GetNumAudioDevices(0); ++i) {
    std::cout << "  [" << i << "] " << SDL_GetAudioDeviceName(i, 0)
              << std::endl;
  }

  if (!params_.out_device.empty())
    std::cout << "Speaker device name: " << params_.out_device << std::endl;
  else
    std::cout << "Using default audio device" << std::endl;

  const char *device_name =
      params_.out_device.empty() ? nullptr : params_.out_device.c_str();
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

void AudioPlayer::receiveAudioData(const rtc::binary &data,
                                   const rtc::FrameInfo &info) {

  uint64_t timestamp_us = info.timestamp;
  uint8_t payloadType = info.payloadType;
  packets_received_++;

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

    // 当队列满时丢弃旧数据包以降低延迟
    if (!decode_queue_.try_push(packet)) {
      av_packet_free(&packet);

      // 每隔一段时间输出警告信息
      static int drop_count = 0;
      if (++drop_count % 100 == 0) {
        std::cout << "Warning: Audio decode queue full, dropping packets. "
                  << "Total drops: " << drop_count << std::endl;
      }
    }
  }
}

void AudioPlayer::setVolume(float volume) {
  volume_ = std::clamp(volume, 0.0f, 1.0f);
}

float AudioPlayer::getVolume() const { return volume_.load(); }

void AudioPlayer::sdlAudioCallback(void *userdata, Uint8 *stream, int len) {
  auto player = static_cast<AudioPlayer *>(userdata);
  player->audioCallback(stream, len);
}

void AudioPlayer::audioCallback(Uint8 *stream, int len) {
  SDL_memset(stream, 0, len);

  int16_t *output_buffer = reinterpret_cast<int16_t *>(stream);
  int samples_needed = len / sizeof(int16_t);

  // 音量控制参数 (0-100%)
  float volume_scale = volume_.load();

  AVFrame *frame = nullptr;
  if (!audio_sample_queue_.pop(frame)) {
    // 队列为空，跳出循环
    return;
  }

  if (!frame || !frame->data[0] || frame->nb_samples <= 0) {
    if (frame)
      av_frame_free(&frame);
    return;
  }

  int16_t *frame_data = reinterpret_cast<int16_t *>(frame->data[0]);
  int samples_available = frame->nb_samples * frame->channels;

  // 确保有足够的音频数据，否则进行欠载处理
  if (samples_available < samples_needed) {
    audio_underruns_++;

    // 减少日志输出频率
    if (audio_underruns_.load() % 200 == 0) {
      std::cout << "Audio underrun #" << audio_underruns_.load()
                << ": requested " << samples_needed << " samples, got "
                << samples_available << std::endl;
    }
  }

  // 一次性批量拷贝并应用音量控制
  //  if (volume_scale == 1.0f) {
  //    // 如果音量为100%，直接内存拷贝
  //    memcpy(output_buffer, frame_data, samples_needed * sizeof(int16_t));
  //  } else {
  //    // 批量处理音量控制
  //    for (int i = 0; i < samples_needed; ++i) {
  //      float scaled_sample = static_cast<float>(frame_data[i]) *
  //      volume_scale; output_buffer[i] = static_cast<int16_t>(
  //          std::max(-32768.0f, std::min(32767.0f, scaled_sample)));
  //    }
  //  }

  av_frame_free(&frame);
}

void AudioPlayer::decodeThread() {
  std::cout << "Audio decode thread started" << std::endl;
  std::string wav_filename = "captured_remote_audio.wav";
  std::string ogg_filename = "captured_remote_audio.ogg";

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
  const bool enable_debug_save = false; // 默认关闭调试保存

  // 标记是否已经初始化了重采样器
  bool resampler_initialized = false;

  // 标记是否已经初始化了SDL音频设备
  bool sdl_initialized = false;

  // 是否初始化了解码器
  bool decoder_initialized = false;

  // 输出音频参数
  int out_sample_rate = params_.out_sample_rate;
  int out_channels = params_.out_channels;
  SDL_AudioFormat out_sample_fmt_sdl = AUDIO_S16;
  AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;

  while (running_) {
    AVPacket *packet = nullptr;
    // 使用带超时的等待，避免线程无法及时退出
    decode_queue_.wait_and_pop(packet);

    if (!running_) {
      if (packet)
        av_packet_free(&packet);
      break;
    }
    if (!decoder_initialized) {
      if (!opus_decoder_->open_decoder()) {
        std::cerr << "Failed to initialize Opus decoder" << std::endl;
        SDL_Quit();
        exit(-100);
      }
    }

    packets_processed_++;
    packets_in_period++;

    if (packet->size > 0 &&
        opus_decoder_->decode_packet(packet, decoded_frame)) {
      // Save OPUS
      //      DebugUtils::save_opus_packet_to_ogg(packet, "opus_packets.ogg");
      // 第一次成功解码后，获取实际参数并初始化SDL音频设备和重采样器
      if (!sdl_initialized) {
        // 初始化SDL音频设备，使用实际参数
        if (!initSDLAudio(out_sample_rate, out_channels, out_sample_fmt_sdl,
                          decoded_frame->nb_samples)) {
          std::cerr << "Failed to initialize SDL audio" << std::endl;
          av_packet_free(&packet);
          break;
        }
        sdl_initialized = true;
      }

      if (!resampler_initialized) {
        // 获取实际解码参数
        int actual_sample_rate = opus_decoder_->get_sample_rate();
        int actual_channels = opus_decoder_->get_channels();
        AVSampleFormat actual_sample_fmt = opus_decoder_->get_sample_fmt();

        uint64_t out_channel_layout =
            av_get_default_channel_layout(out_channels);
        uint64_t in_channel_layout =
            av_get_default_channel_layout(actual_channels);

        // 配置重采样器：保持原始格式
        swr_ctx_ = swr_alloc_set_opts(nullptr,
                                      // 输出格式 - 与输入相同
                                      out_channel_layout, out_sample_fmt,
                                      out_sample_rate,
                                      // 输入格式（Opus解码器输出）
                                      in_channel_layout, actual_sample_fmt,
                                      actual_sample_rate, 0, nullptr);

        if (!swr_ctx_) {
          std::cerr << "Failed to allocate resampler context" << std::endl;
        } else {
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
            std::cout << "Volume: " << params_.volume << std::endl;
          }
        }
      }

      if (resampler_initialized) {
        // 获取新的实际解码参数
        AVSampleFormat actual_sample_fmt = opus_decoder_->get_sample_fmt();
        int actual_sample_rate = opus_decoder_->get_sample_rate();
        int actual_channels = opus_decoder_->get_channels();
        // 设置输出帧参数
        resampled_frame->channel_layout =
            av_get_default_channel_layout(out_channels);
        resampled_frame->sample_rate = out_sample_rate;
        resampled_frame->format = out_sample_fmt; // 强制输出为S16格式

        int ret = swr_convert_frame(swr_ctx_, resampled_frame, decoded_frame);
        if (ret == AVERROR_INPUT_CHANGED) {
          std::cerr << "Audio resampling context needs reinitialization due to "
                       "input change"
                    << std::endl;
          // 重新初始化重采样器
          swr_free(&swr_ctx_);
          swr_ctx_ = nullptr;

          swr_ctx_ = swr_alloc_set_opts(
              nullptr,
              // 输出格式
              av_get_default_channel_layout(out_channels), out_sample_fmt,
              out_sample_rate,
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
          char err_str[AV_ERROR_MAX_STRING_SIZE] = {0};
          av_strerror(ret, err_str, sizeof(err_str));
          std::cerr << "Failed to resample audio frame: " << ret << " ("
                    << err_str << ")" << std::endl;
        } else {
          // 将重采样后的帧放入队列
          DebugUtils::save_raw_audio_frame2(resampled_frame, wav_filename);
          AVFrame *queue_frame = av_frame_alloc();
          if (queue_frame) {
            av_frame_move_ref(queue_frame, resampled_frame);

            // 使用非阻塞推送
            if (!audio_sample_queue_.try_push(queue_frame)) {
              // 队列满时丢弃帧以降低延迟
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

  DebugUtils::finalize_raw_audio_frame_file2();
  //  DebugUtils::finalize_opus_ogg_file();
  av_frame_free(&decoded_frame);
  av_frame_free(&resampled_frame);

  std::cout << "Audio decode thread stopped" << std::endl;
}