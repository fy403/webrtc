#include "encoder.h"
#ifndef H264_ENCODER_H
#define H264_ENCODER_H
class H264Encoder : public Encoder {
public:
  H264Encoder(bool debug_enabled = false, const std::string &codec_name = "libx264");
  ~H264Encoder();

  bool open_encoder(int width, int height, int fps, int64_t bit_rate,
                   const std::string &profile = "lowlatency") override;
  void close_encoder() override;

  AVCodecContext *get_context() const override { return encoder_context_; }

  bool encode_frame(AVFrame *frame, AVPacket *packet) override;

private:
  bool debug_enabled_;
  std::string codec_name_;    // 编码器名称 (libx264, h264_rkmpp 等)
  AVCodecContext *encoder_context_;
  const AVCodec *codec_;

  // 编码帧计数（每次 open_encoder 时重置）
  int64_t frame_count_ = 0;
  int64_t keyframe_count_ = 0;
  int64_t last_keyframe_pts_ = -1;
};
#endif // H264_ENCODER_H
