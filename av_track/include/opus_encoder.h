#ifndef OPUS_ENCODER_H
#define OPUS_ENCODER_H

#include <memory>
#include <string>

#include "encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

class OpusEncoder : public Encoder {
public:
  OpusEncoder(bool debug_enabled = false);
  ~OpusEncoder();

  bool open_encoder(int sample_rate, int channels,
                    AVSampleFormat format) override;
  void close_encoder() override;

  AVCodecContext *get_context() const override { return encoder_context_; }

  bool encode_frame(AVFrame *frame, AVPacket *packet) override;
  
  // 新增方法，用于向 FIFO 写入数据
  bool write_to_fifo(AVFrame *frame);
  
  // 新增方法，从 FIFO 读取数据
  int read_from_fifo(AVFrame *frame);

private:
  bool debug_enabled_;
  AVCodecContext *encoder_context_;
  const AVCodec *codec_;
  int frame_count_;
  
  // 添加音频 FIFO 相关成员
  AVAudioFifo *audio_fifo_;
  int fifo_frame_size_;  // FIFO中每个帧的样本数
};

#endif // OPUS_ENCODER_H