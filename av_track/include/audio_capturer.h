#ifndef AUDIO_CAPTURER_H
#define AUDIO_CAPTURER_H

#include "capture.h"
#include <memory>
#include <string>

// Forward declarations
class Encoder;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 音频设备参数结构
struct AudioDeviceParams {
  std::string device;
  int sample_rate;
  int channels;
  std::string input_format;

  AudioDeviceParams() : sample_rate(0), channels(0), input_format("") {}

  bool is_valid() const { return !device.empty(); }
};

namespace rtc {
class Track;
}

class AudioCapturer : public Capture {
public:
  AudioCapturer(const AudioDeviceParams &params, bool debug_enabled = false,
                size_t decode_queue_capacity = 512,
                size_t encode_queue_capacity = 512,
                size_t send_queue_capacity = 512);
  ~AudioCapturer();

  bool start() override;
  void stop() override;

private:
  void capture_loop() override;
  void decode_loop() override;
  void encode_loop() override;
  void send_loop() override;

  AudioDeviceParams audio_params_;
  AVFormatContext *format_context_ = nullptr;
  AVCodecContext *codec_context_ = nullptr;
  int audio_stream_index_ = -1;

  std::shared_ptr<rtc::Track> track_;
};

#endif // AUDIO_CAPTURER_H