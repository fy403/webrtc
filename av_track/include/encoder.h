#ifndef ENCODER_H
#define ENCODER_H

#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

class Encoder {
public:
  virtual ~Encoder() = default;

  // 视频编码器接口
  virtual bool open_encoder(int width, int height, int fps, int64_t bit_rate) {
    // 默认实现，对于视频编码器
    return false;
  }

  // 音频编码器接口
  virtual bool open_encoder(int sample_rate, int channels,
                            AVSampleFormat format) {
    // 默认实现，对于音频编码器
    return false;
  }

  virtual void close_encoder() = 0;

  virtual AVCodecContext *get_context() const = 0;

  virtual bool encode_frame(AVFrame *frame, AVPacket *packet) = 0;

protected:
  Encoder() = default;
};
#endif // ENCODER_H