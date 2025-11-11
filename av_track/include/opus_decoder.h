#ifndef OPUS_DECODER_H
#define OPUS_DECODER_H

#include "decoder.h"
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

class OpusDecoder : public Decoder {
public:
  OpusDecoder();
  virtual ~OpusDecoder();

  // 新增方法：在不知道确切参数的情况下打开解码器
  bool open_decoder();
  
  bool open_decoder(int sample_rate, int channels,
                    AVSampleFormat format) override;
  void close_decoder() override;

  AVCodecContext *get_context() const override { return decoder_context_; }

  bool decode_packet(AVPacket *packet, AVFrame *frame) override;
  
  // 新增方法：获取解码后的参数
  int get_sample_rate() const;
  int get_channels() const;
  AVSampleFormat get_sample_fmt() const;

private:
  AVCodecContext *decoder_context_;
  const AVCodec *codec_;
  
  // 存储实际解码参数
  int actual_sample_rate_;
  int actual_channels_;
  AVSampleFormat actual_sample_fmt_;
};

#endif // OPUS_DECODER_H