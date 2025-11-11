#include "audio_player.h"
#include <chrono>
#include <cstring>
#include <iostream>

AudioPlayer::AudioPlayer(const std::string &audio_device)
    : audio_device_(audio_device),
      opus_decoder_(std::make_unique<OpusDecoder>()), decode_queue_(512),
      audio_sample_queue_(512) {
  decode_queue_.set_deleter([](AudioPacket &packet) { packet.data.clear(); });
  audio_sample_queue_.set_deleter(
      [](AudioSample &sample) { sample.data.clear(); });
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
  if (!opus_decoder_->open_decoder(48000, 1, AV_SAMPLE_FMT_S16)) {
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

  std::cout << "Audio player stopped" << std::endl;
}

void AudioPlayer::receiveAudioData(const rtc::binary &data) {
  AudioPacket packet;
  packet.data.assign(reinterpret_cast<const unsigned char *>(data.data()),
                     reinterpret_cast<const unsigned char *>(data.data()) +
                         data.size());
  packet.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

  decode_queue_.push(std::move(packet));
}

bool AudioPlayer::initSDLAudio() {
  SDL_AudioSpec desired, obtained;
  SDL_zero(desired);

  desired.freq = 48000;
  desired.format = AUDIO_S16;
  desired.channels = 1;
  desired.samples = 1024; // 约21ms的缓冲
  desired.callback = &AudioPlayer::sdlAudioCallback;
  desired.userdata = this;

  const char *device_name =
      audio_device_.empty() ? nullptr : audio_device_.c_str();
  audio_device_id_ =
      SDL_OpenAudioDevice(device_name, 0, &desired, &obtained, 0);

  if (audio_device_id_ == 0) {
    std::cerr << "Failed to open audio device( " << device_name
              << "): " << SDL_GetError() << std::endl;
    return false;
  }

  audio_spec_ = obtained;

  std::cout << "SDL audio initialized: " << obtained.freq << "Hz, "
            << static_cast<int>(obtained.channels) << " channels, "
            << obtained.samples << " samples" << std::endl;

  // 启动SDL音频播放
  SDL_PauseAudioDevice(audio_device_id_, 0);
  // 打印播放设备的名称
  std::cout << "Playing on device: "
            << SDL_GetAudioDeviceName(audio_device_id_, 0);
  return true;
}

void AudioPlayer::cleanup() {
  if (audio_device_id_ != 0) {
    // 停止音频播放
    SDL_PauseAudioDevice(audio_device_id_, 1);
    SDL_CloseAudioDevice(audio_device_id_);
    audio_device_id_ = 0;
  }

  if (opus_decoder_) {
    opus_decoder_->close_decoder();
  }
}

void AudioPlayer::sdlAudioCallback(void *userdata, Uint8 *stream, int len) {
  auto player = static_cast<AudioPlayer *>(userdata);
  player->audioCallback(stream, len);
}

void AudioPlayer::audioCallback(Uint8 *stream, int len) {
  // 清空输出流
  SDL_memset(stream, 0, len);

  size_t total_read = 0;
  while (total_read < static_cast<size_t>(len)) {
    // 尝试从音频样本队列中获取数据
    AudioSample sample;
    if (!audio_sample_queue_.pop(sample)) {
      // 队列为空，跳出循环
      break;
    }

    // 计算可以复制的数据量
    size_t to_copy =
        std::min(static_cast<size_t>(len) - total_read, sample.size);

    // 复制数据到输出流
    memcpy(stream + total_read, sample.data.data(), to_copy);

    total_read += to_copy;

    // 如果还有剩余数据，重新放回队列
    if (to_copy < sample.size) {
      AudioSample remaining;
      remaining.size = sample.size - to_copy;
      remaining.data.resize(remaining.size);
      memcpy(remaining.data.data(), sample.data.data() + to_copy,
             remaining.size);
      audio_sample_queue_.try_push(std::move(remaining));
      break;
    }
  }
}

void AudioPlayer::decodeThread() {
  std::cout << "Audio decode thread started" << std::endl;

  AVPacket *packet = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();

  if (!packet || !frame) {
    std::cerr << "Failed to allocate FFmpeg resources" << std::endl;
    if (packet)
      av_packet_free(&packet);
    if (frame)
      av_frame_free(&frame);
    return;
  }

  while (running_) {
    AudioPacket audio_packet;
    decode_queue_.wait_and_pop(audio_packet);

    if (!running_)
      break;

    // 准备解码数据包
    packet->data = audio_packet.data.data();
    packet->size = static_cast<int>(audio_packet.data.size());

    // 使用 Opus 解码器解码数据包
    if (opus_decoder_->decode_packet(packet, frame)) {
      // 将解码后的帧写入 FIFO
      if (opus_decoder_->write_to_fifo(frame)) {
        // 从 FIFO 读取完整帧并放入音频样本队列
        auto output_frame =
            std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *f) {
              if (f) {
                av_frame_free(&f);
              }
            });

        while (opus_decoder_->read_from_fifo(output_frame.get()) > 0) {
          if (output_frame && output_frame->format == AV_SAMPLE_FMT_S16 &&
              output_frame->channels == 1) {
            int data_size = output_frame->nb_samples * output_frame->channels *
                            sizeof(int16_t);

            // 创建音频样本并放入队列
            AudioSample sample;
            sample.size = data_size;
            sample.data.resize(data_size);
            memcpy(sample.data.data(), output_frame->data[0], data_size);

            audio_sample_queue_.try_push(std::move(sample));
          }
        }
      }
    }

    av_packet_unref(packet);
  }

  av_packet_free(&packet);
  av_frame_free(&frame);

  std::cout << "Audio decode thread stopped" << std::endl;
}