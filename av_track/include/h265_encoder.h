#ifndef H265_ENCODER_H
#define H265_ENCODER_H

#include "encoder.h"

class H265Encoder : public Encoder {
public:
  H265Encoder(bool debug_enabled = false);
  ~H265Encoder();

  bool open_encoder(int width, int height, int fps, int64_t bit_rate) override;
  void close_encoder() override;

  AVCodecContext *get_context() const override { return encoder_context_; }

  bool encode_frame(AVFrame *frame, AVPacket *packet) override;

private:
  bool debug_enabled_;
  AVCodecContext *encoder_context_;
  const AVCodec *codec_;
};
#endif // H265_ENCODER_H
