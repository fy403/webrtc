#include "encoder.h"
#ifndef H264_ENCODER_H
#define H264_ENCODER_H
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
#endif // H264_ENCODER_H
