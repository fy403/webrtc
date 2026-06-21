#ifndef H265_ENCODER_H
#define H265_ENCODER_H

#include "encoder.h"

class H265Encoder : public Encoder {
public:
  H265Encoder(bool debug_enabled = false, const std::string &codec_name = "libx265");
  ~H265Encoder();

  bool open_encoder(int width, int height, int fps, int64_t bit_rate,
                   const std::string &profile = "lowlatency") override;
  void close_encoder() override;

  AVCodecContext *get_context() const override { return encoder_context_; }

  bool encode_frame(AVFrame *frame, AVPacket *packet) override;

private:
  bool debug_enabled_;
  std::string codec_name_;    // 编码器名称 (libx265, hevc_rkmpp 等)
  AVCodecContext *encoder_context_;
  const AVCodec *codec_;

  // 编码帧计数（每次 open_encoder 时重置）
  int64_t frame_count_ = 0;
};
#endif // H265_ENCODER_H
