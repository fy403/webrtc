#include "audio_player.h"
#include "debug_utils.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>

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

  // 初始化 Opus 解码器
  if (!opus_decoder_->open_decoder(params_.sample_rate, params_.channels,
                                   AV_SAMPLE_FMT_S16)) {
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

  desired.freq = params_.sample_rate;
  desired.format = AUDIO_S16;
  desired.channels = params_.channels;
  desired.samples = 1024; // 优化缓冲区大小
  desired.callback = &AudioPlayer::sdlAudioCallback;
  desired.userdata = this;

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

void AudioPlayer::cleanup() {
  if (audio_device_id_ != 0) {
    SDL_PauseAudioDevice(audio_device_id_, 1);
    SDL_CloseAudioDevice(audio_device_id_);
    audio_device_id_ = 0;
  }

  if (opus_decoder_) {
    opus_decoder_->close_decoder();
  }
}

void AudioPlayer::receiveAudioData(const rtc::binary &data) {
  packets_received_++;

  // 性能统计：每100个包输出一次
  static auto last_stat_time = std::chrono::steady_clock::now();
  static int local_packet_count = 0;

  local_packet_count++;
  auto now = std::chrono::steady_clock::now();
  if (now - last_stat_time > std::chrono::seconds(2)) {
    std::cout << "Audio receive rate: " << local_packet_count / 2.0
              << " packets/s" << std::endl;
    local_packet_count = 0;
    last_stat_time = now;
  }

  // 检查 RTP 包
  if (data.size() >= sizeof(rtc::RtpHeader) &&
      (std::to_integer<uint8_t>(data[0]) == 0x80 ||
       std::to_integer<uint8_t>(data[0]) == 0x81)) {

    auto rtp = reinterpret_cast<const rtc::RtpHeader *>(data.data());
    if (rtp->version() != 2) {
      return;
    }

    const char *payload = rtp->getBody();
    size_t header_size = payload - reinterpret_cast<const char *>(data.data());
    size_t payload_size = data.size() - header_size;

    if (payload_size > 0) {
      AVPacket *packet = av_packet_alloc();
      if (packet) {
        packet->data =
            const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(payload));
        packet->size = payload_size;
        packet->buf = av_buffer_create(
            packet->data, packet->size, [](void *, uint8_t *) {}, nullptr, 0);
        packet->pts = rtp->timestamp();
        packet->dts = rtp->timestamp();

        // 使用非阻塞推送
        if (!decode_queue_.try_push(packet)) {
          av_packet_free(&packet);
          // 队列满时丢弃数据包，避免积压
        }
      }
    }
    return;
  }

  // 处理非RTP数据
  AVPacket *packet = av_packet_alloc();
  if (packet) {
    packet->data =
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(data.data()));
    packet->size = data.size();
    packet->buf = av_buffer_create(
        packet->data, packet->size, [](void *, uint8_t *) {}, nullptr, 0);

    static uint64_t default_timestamp = 0;
    packet->pts = default_timestamp;
    packet->dts = default_timestamp;
    default_timestamp++;

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

  size_t total_written = 0;
  int frames_used = 0;
  const int max_frames_per_callback = 8; // 限制每次回调最多使用的帧数

  while (total_written < static_cast<size_t>(len) &&
         frames_used < max_frames_per_callback) {
    AVFrame *frame = nullptr;
    if (!audio_sample_queue_.pop(frame)) {
      break;
    }

    if (!frame || !frame->data[0] || frame->nb_samples == 0) {
      if (frame)
        av_frame_free(&frame);
      continue;
    }

    size_t frame_size = frame->nb_samples * frame->channels * sizeof(int16_t);
    size_t copy_size =
        std::min(frame_size, static_cast<size_t>(len) - total_written);

    memcpy(stream + total_written, frame->data[0], copy_size);
    total_written += copy_size;
    frames_used++;

    // 处理未用完的帧数据
    if (copy_size < frame_size) {
      AVFrame *remaining_frame = av_frame_alloc();
      if (remaining_frame) {
        av_frame_ref(remaining_frame, frame);
        size_t samples_used = copy_size / (frame->channels * sizeof(int16_t));
        remaining_frame->data[0] = frame->data[0] + copy_size;
        remaining_frame->nb_samples = frame->nb_samples - samples_used;

        // 尝试放回队列，如果失败则丢弃
        if (!audio_sample_queue_.try_push(remaining_frame)) {
          av_frame_free(&remaining_frame);
        }
      }
    }

    av_frame_free(&frame);
  }

  // 处理音频欠载
  if (total_written < static_cast<size_t>(len)) {
    audio_underruns_++;

    // 每20次欠载输出一次警告
    //    if (audio_underruns_.load() % 20 == 0) {
    //      std::cout << "Audio underrun #" << audio_underruns_.load()
    //                << ": requested " << len << " bytes, got " <<
    //                total_written
    //                << std::endl;
    //    }
  }
}

void AudioPlayer::decodeThread() {
  std::cout << "Audio decode thread started" << std::endl;

  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    std::cerr << "Failed to allocate frame" << std::endl;
    return;
  }

  int processed_count = 0;
  auto last_log_time = std::chrono::steady_clock::now();
  auto last_perf_time = last_log_time;
  int packets_in_period = 0;

  // 只在调试时保存音频文件
  const bool enable_debug_save = false;
  std::string frame_filename = "captured_remote_audio_frames.wav";
  auto start_time = std::chrono::steady_clock::now();

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

    // 简化处理逻辑
    if (packet->size > 0 && opus_decoder_->decode_packet(packet, frame)) {
      AVFrame *queue_frame = av_frame_alloc();
      if (queue_frame) {
        av_frame_move_ref(queue_frame, frame);

        // 使用非阻塞推送
        if (!audio_sample_queue_.try_push(queue_frame)) {
          // 队列满时丢弃帧
          av_frame_free(&queue_frame);
        }
      }

      // 调试保存（可选）
      if (enable_debug_save) {
        auto current_time = std::chrono::steady_clock::now();
        if (current_time - start_time <= std::chrono::seconds(5)) {
          DebugUtils::save_raw_audio_frame(frame, frame_filename);
        }
      }
    }

    av_packet_free(&packet);

    // 性能统计和日志输出
    auto now = std::chrono::steady_clock::now();
    if (now - last_perf_time > std::chrono::seconds(5)) {
      std::cout << "Audio decode: " << packets_in_period / 5.0 << " packets/s, "
                << "queue sizes: decode=" << decode_queue_.size()
                << ", audio=" << audio_sample_queue_.size() << std::endl;
      packets_in_period = 0;
      last_perf_time = now;
    }
  }

  av_frame_free(&frame);

  if (enable_debug_save) {
    DebugUtils::finalize_raw_audio_frame_file();
  }

  std::cout << "Audio decode thread stopped" << std::endl;
}