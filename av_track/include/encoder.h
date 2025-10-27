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
  virtual bool open_encoder(int width, int height, int fps) {
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

class H264Encoder : public Encoder {
public:
  H264Encoder(bool debug_enabled = false);
  ~H264Encoder();

  bool open_encoder(int width, int height, int fps) override;
  void close_encoder() override;

  AVCodecContext *get_context() const override { return encoder_context_; }

  bool encode_frame(AVFrame *frame, AVPacket *packet) override;

private:
  bool debug_enabled_;
  AVCodecContext *encoder_context_;
  const AVCodec *codec_;
};

#endif // ENCODER_H