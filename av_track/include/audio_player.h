#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "rtc/rtc.hpp"
#include "safe_queue.h"
#include "opus_decoder.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

struct AudioPacket {
  std::vector<uint8_t> data;
  uint64_t timestamp;
};

// 定义音频样本结构
struct AudioSample {
  std::vector<uint8_t> data;  // PCM数据
  size_t size;                // 数据大小
};

class AudioPlayer {
public:
  explicit AudioPlayer(const std::string &audio_device = "");
  ~AudioPlayer();

  void start();
  void stop();
  void receiveAudioData(const rtc::binary &data);
  bool isRunning() const { return running_; }

private:
  void decodeThread();
  bool initSDLAudio();
  void cleanup();

  static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);
  void audioCallback(Uint8 *stream, int len);

  std::string audio_device_;
  std::atomic<bool> running_{false};

  std::thread decode_thread_;
  SafeQueue<AudioPacket> decode_queue_;

  // Opus 解码器
  std::unique_ptr<OpusDecoder> opus_decoder_;

  // SDL 音频相关
  SDL_AudioDeviceID audio_device_id_ = 0;
  SDL_AudioSpec audio_spec_;
  
  // 音频样本队列
  SafeQueue<AudioSample> audio_sample_queue_;
};

#endif // AUDIO_PLAYER_H