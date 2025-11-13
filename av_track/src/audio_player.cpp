#include "audio_player.h"
#include "debug_utils.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>

AudioPlayer::AudioPlayer(const std::string &audio_device,
                         const AudioDeviceParams &params)
    : audio_device_(audio_device), params_(params),
      opus_decoder_(std::make_unique<OpusDecoder>()), decode_queue_(512),
      audio_sample_queue_(512) {
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

  std::cout << "Audio player stopped" << std::endl;
}

void AudioPlayer::receiveAudioData(const rtc::binary &data) {
  std::cout << "Received audio data, size: " << data.size() << std::endl;

  // Check if this is an RTP packet (starts with version 2 - 0x80 or 0x81)
  if (data.size() >= sizeof(rtc::RtpHeader) &&
      (std::to_integer<uint8_t>(data[0]) == 0x80 ||
       std::to_integer<uint8_t>(data[0]) == 0x81)) {

    std::cout << "Processing RTP packet..." << std::endl;

    // Use libdatachannel's RTP helper class
    auto rtp = reinterpret_cast<const rtc::RtpHeader *>(data.data());

    // Validate RTP header
    if (rtp->version() != 2) {
      std::cerr << "Invalid RTP version: " << (int)rtp->version() << std::endl;
      return;
    }

    // Get the payload start position
    const char *payload = rtp->getBody();
    size_t header_size = payload - reinterpret_cast<const char *>(data.data());
    size_t payload_size = data.size() - header_size;

    std::cout << "RTP header size: " << header_size
              << ", payload size: " << payload_size << std::endl;

    if (payload_size > 0) {
      // Log first few bytes of payload for debugging
      std::cout << "Payload first 16 bytes: ";
      const unsigned char *payload_bytes =
          reinterpret_cast<const unsigned char *>(payload);
      for (size_t i = 0; i < std::min(payload_size, size_t(16)); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)payload_bytes[i] << " ";
      }
      std::cout << std::dec << std::endl;

      // 创建 AVPacket 并放入队列
      AVPacket *packet = av_packet_alloc();
      if (packet) {
        packet->data =
            const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(payload));
        packet->size = payload_size;
        // 使用引用计数确保数据在使用期间不被释放
        packet->buf = av_buffer_create(
            packet->data, packet->size, [](void *, uint8_t *) {}, nullptr, 0);

        // 设置时间戳以避免警告
        packet->pts = rtp->timestamp();
        packet->dts = rtp->timestamp();

        std::cout << "Sending packet to decode queue, payload size: "
                  << packet->size << std::endl;
        decode_queue_.push(packet);
      }
      return;
    } else {
      std::cerr << "RTP packet has no payload" << std::endl;
      return;
    }
  }

  // If not RTP or couldn't parse, treat as raw data
  std::cout << "Treating as raw data" << std::endl;
  AVPacket *packet = av_packet_alloc();
  if (packet) {
    packet->data =
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(data.data()));
    packet->size = data.size();
    // 使用引用计数确保数据在使用期间不被释放
    packet->buf = av_buffer_create(
        packet->data, packet->size, [](void *, uint8_t *) {}, nullptr, 0);

    // 设置默认时间戳
    static uint64_t default_timestamp = 0;
    packet->pts = default_timestamp;
    packet->dts = default_timestamp;
    default_timestamp++;

    decode_queue_.push(packet);
  }
}

bool AudioPlayer::initSDLAudio() {
  SDL_AudioSpec desired, obtained;
  SDL_zero(desired);

  desired.freq = 48000;
  desired.format = AUDIO_S16;
  desired.channels = 1;
  desired.samples = 2048; // 增加缓冲区大小以减少欠载
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
  // Validate inputs
  if (!stream || len <= 0) {
    std::cerr << "Invalid audio callback parameters: stream=" << stream
              << ", len=" << len << std::endl;
    return;
  }

  // Clear output stream
  SDL_memset(stream, 0, len);

  size_t total_read = 0;
  while (total_read < static_cast<size_t>(len)) {
    // Try to get data from audio sample queue
    AVFrame *frame = nullptr;
    if (!audio_sample_queue_.pop(frame)) {
      // Queue is empty, break out of loop
      break;
    }

    // Validate sample
    if (frame->nb_samples == 0 || !frame->data[0]) {
      std::cerr << "Invalid sample: nb_samples=" << frame->nb_samples
                << ", data[0]=" << frame->data[0] << std::endl;
      av_frame_free(&frame);
      continue;
    }

    // Calculate amount of data that can be copied
    size_t frame_data_size =
        frame->nb_samples * frame->channels * sizeof(short); // S16 format
    size_t to_copy =
        std::min(static_cast<size_t>(len) - total_read, frame_data_size);

    // Copy data to stream
    if (to_copy > 0 && frame->data[0]) {
      memcpy(stream + total_read, frame->data[0], to_copy);
    }

    total_read += to_copy;

    // If there's remaining data in the frame, keep the frame for next callback
    if (to_copy < frame_data_size) {
      // Calculate remaining samples
      size_t consumed_bytes = to_copy;
      size_t remaining_bytes = frame_data_size - consumed_bytes;
      size_t remaining_samples =
          remaining_bytes / (frame->channels * sizeof(short));

      // Modify the existing frame to represent the remaining data
      memmove(frame->data[0], frame->data[0] + consumed_bytes, remaining_bytes);
      frame->nb_samples = remaining_samples;

      // Put the modified frame back to the queue for next callback
      audio_sample_queue_.try_push(frame);
    } else {
      // We've used all the data, free the frame
      av_frame_free(&frame);
    }
  }
}

void AudioPlayer::decodeThread() {
  static auto start_time = std::chrono::steady_clock::now();
  AVFrame *frame = av_frame_alloc();

  if (!frame) {
    std::cerr << "Failed to allocate FFmpeg resources" << std::endl;
    if (frame)
      av_frame_free(&frame);
    return;
  }

  while (running_) {
    try {
      AVPacket *packet = nullptr;
      std::cout << "Waiting for audio packet..." << std::endl;
      decode_queue_.wait_and_pop(packet);
      std::cout << "Got audio packet from queue, size: " << packet->size
                << std::endl;

      if (!running_)
        break;

      // Check for minimum valid Opus packet size
      if (packet->size == 0) {
        std::cerr << "Received empty audio packet, skipping" << std::endl;
        av_packet_free(&packet);
        continue;
      }

      std::cout << "Attempting to decode packet of size: " << packet->size
                << std::endl;
      // Use Opus decoder to decode packet
      if (opus_decoder_->decode_packet(packet, frame)) {
        std::cout << "Packet decoded successfully" << std::endl;

        // 直接将解码后的帧放入audio_sample_queue_
        AVFrame *queue_frame = av_frame_alloc();
        if (queue_frame) {
          // 复制frame的内容到queue_frame
          av_frame_move_ref(queue_frame, frame);

          // 将frame放入队列
          audio_sample_queue_.push(queue_frame);
          std::cout << "Frame written directly to audio sample queue, samples: "
                    << queue_frame->nb_samples << std::endl;
        }
      } else {
        std::cerr << "Failed to decode packet" << std::endl;
      }

      av_packet_free(&packet);
    } catch (const std::exception &e) {
      std::cerr << "Exception in decode thread: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "Unknown exception in decode thread" << std::endl;
    }
  }

  av_frame_free(&frame);

  std::cout << "Audio decode thread stopped" << std::endl;
}