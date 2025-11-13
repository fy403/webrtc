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

  bool open_decoder(int sample_rate, int channels,
                    AVSampleFormat format) override;
  void close_decoder() override;

  AVCodecContext *get_context() const override { return decoder_context_; }

  bool decode_packet(AVPacket *packet, AVFrame *frame) override;

private:
  AVCodecContext *decoder_context_;
  const AVCodec *codec_;
};

#endif // OPUS_DECODER_H