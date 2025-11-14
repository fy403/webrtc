#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "audio_capturer.h"
#include "opus_decoder.h"
#include "rtc/rtc.hpp"
#include "safe_queue.h"
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

struct AudioPlayerDeviceParams {
  std::string out_device;
  int out_sample_rate;
  int out_channels;
  float volume;

  AudioPlayerDeviceParams()
      : out_sample_rate(48000), out_channels(2), volume(0.45f), out_device("") {
  }
};

class AudioPlayer {
public:
  explicit AudioPlayer(const AudioPlayerDeviceParams &params);
  ~AudioPlayer();

  void start();
  void stop();
  void receiveAudioData(const rtc::binary &data, const rtc::FrameInfo &info);
  bool isRunning() const { return running_; }

  // 音量控制接口
  void setVolume(float volume); // 设置音量 (0.0 - 1.0)
  float getVolume() const;      // 获取当前音量

private:
  void decodeThread();
  bool initSDLAudio(int sample_rate, int channels, SDL_AudioFormat format,
                    Uint16 frame_size);
  void cleanup();

  static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);
  void audioCallback(Uint8 *stream, int len);

  AudioPlayerDeviceParams params_;
  std::atomic<bool> running_{false};

  std::thread decode_thread_;
  SafeQueue<AVPacket *> decode_queue_;

  // Opus 解码器
  std::unique_ptr<OpusDecoder> opus_decoder_;

  // SDL 音频相关
  SDL_AudioDeviceID audio_device_id_ = 0;
  SDL_AudioSpec audio_spec_;

  // 添加重采样相关成员
  SwrContext *swr_ctx_ = nullptr;
  // 音频样本队列
  SafeQueue<AVFrame *> audio_sample_queue_;

  // 性能统计
  std::atomic<int> packets_received_{0};
  std::atomic<int> packets_processed_{0};
  std::atomic<int> audio_underruns_{0};

  // 音量控制
  std::atomic<float> volume_{0.45f}; // 音量 (0.0 - 1.0)
};

#endif // AUDIO_PLAYER_H