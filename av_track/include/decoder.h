#ifndef DECODER_H
#define DECODER_H

#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

class Decoder {
public:
  virtual ~Decoder() = default;

  // 音频解码器接口
  virtual bool open_decoder(int sample_rate, int channels,
                            AVSampleFormat format) {
    // 默认实现，对于音频解码器
    return false;
  }

  virtual void close_decoder() = 0;

  virtual AVCodecContext *get_context() const = 0;

  virtual bool decode_packet(AVPacket *packet, AVFrame *frame) = 0;
  
  // FIFO相关接口
  virtual bool write_to_fifo(AVFrame *frame) = 0;
  virtual int read_from_fifo(AVFrame *frame) = 0;
  virtual int get_fifo_size() const = 0;

protected:
  Decoder() = default;
};
#endif // DECODER_H