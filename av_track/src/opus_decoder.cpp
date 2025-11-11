#include "opus_decoder.h"
#include <iostream>
#include <iomanip>  // Needed for std::setw and std::setfill

extern std::string av_error_string(int errnum);

OpusDecoder::OpusDecoder()
    : decoder_context_(nullptr), codec_(nullptr), audio_fifo_(nullptr),
      fifo_frame_size_(0) {}

OpusDecoder::~OpusDecoder() { close_decoder(); }

bool OpusDecoder::open_decoder(int sample_rate, int channels,
                               AVSampleFormat sample_fmt) {
  codec_ = avcodec_find_decoder(AV_CODEC_ID_OPUS);
  if (!codec_) {
    std::cerr << "Cannot find Opus decoder" << std::endl;
    return false;
  }

  decoder_context_ = avcodec_alloc_context3(codec_);
  if (!decoder_context_) {
    std::cerr << "Cannot allocate Opus decoder context" << std::endl;
    return false;
  }

  // Opus 解码器标准配置
  decoder_context_->sample_rate = sample_rate;
  decoder_context_->channels = channels;
  decoder_context_->channel_layout = av_get_default_channel_layout(channels);
  decoder_context_->sample_fmt = sample_fmt;

  int ret = avcodec_open2(decoder_context_, codec_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot open Opus decoder: " << av_error_string(ret)
              << std::endl;
    return false;
  }

  // 初始化音频 FIFO
  audio_fifo_ = av_audio_fifo_alloc(sample_fmt, channels, 1024);
  if (!audio_fifo_) {
    std::cerr << "Cannot allocate audio FIFO" << std::endl;
    return false;
  }

  // 保存帧大小
  fifo_frame_size_ = decoder_context_->frame_size ? decoder_context_->frame_size
                                                  : 960; // Opus标准帧大小

  return true;
}

bool OpusDecoder::decode_packet(AVPacket *packet, AVFrame *frame) {
  if (!decoder_context_) {
    std::cerr << "Decoder not initialized" << std::endl;
    return false;
  }

  // 发送数据包到解码器
  int ret = avcodec_send_packet(decoder_context_, packet);
  if (ret < 0) {
    if (ret != AVERROR(EAGAIN)) {
      // Try to provide more context about the error
      char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
      av_strerror(ret, errBuf, sizeof(errBuf));
      std::cerr << "Error sending packet for decoding: " << errBuf << " (" << ret << ")" << std::endl;
      
      // Print first few bytes of the packet for debugging
      std::cerr << "Packet data (first 16 bytes): ";
      for (int i = 0; i < std::min(packet->size, 16); ++i) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)packet->data[i] << " ";
      }
      std::cerr << std::dec << std::endl;
    }
    return false;
  }

  // 接收解码后的帧
  ret = avcodec_receive_frame(decoder_context_, frame);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return false;
  } else if (ret < 0) {
    std::cerr << "Error during decoding: " << av_error_string(ret) << std::endl;
    return false;
  }

  return true;
}

bool OpusDecoder::write_to_fifo(AVFrame *frame) {
  if (!audio_fifo_) {
    std::cerr << "Audio FIFO not initialized" << std::endl;
    return false;
  }

  // 将数据写入 FIFO
  int written =
      av_audio_fifo_write(audio_fifo_, (void **)frame->data, frame->nb_samples);
  if (written != frame->nb_samples) {
    std::cerr << "Could not write all samples to FIFO" << std::endl;
    return false;
  }

  return true;
}

int OpusDecoder::read_from_fifo(AVFrame *frame) {
  if (!audio_fifo_) {
    std::cerr << "Audio FIFO not initialized" << std::endl;
    return -1;
  }

  // 检查 FIFO 中是否有足够的数据
  int available_samples = av_audio_fifo_size(audio_fifo_);
  if (available_samples < fifo_frame_size_) {
    // FIFO 中数据不足一个完整的帧
    return 0;
  }

  // 从 FIFO 读取数据到帧中
  int read_samples =
      av_audio_fifo_read(audio_fifo_, (void **)frame->data, fifo_frame_size_);
  if (read_samples != fifo_frame_size_) {
    std::cerr << "Could not read complete frame from FIFO" << std::endl;
    return -1;
  }

  // 设置帧的属性
  frame->nb_samples = fifo_frame_size_;
  frame->channels = decoder_context_->channels;
  frame->channel_layout = decoder_context_->channel_layout;
  frame->sample_rate = decoder_context_->sample_rate;
  frame->format = decoder_context_->sample_fmt;

  return read_samples;
}

int OpusDecoder::get_fifo_size() const {
  if (!audio_fifo_) {
    return 0;
  }
  return av_audio_fifo_size(audio_fifo_);
}

void OpusDecoder::close_decoder() {
  if (decoder_context_) {
    avcodec_free_context(&decoder_context_);
    decoder_context_ = nullptr;
  }

  if (audio_fifo_) {
    av_audio_fifo_free(audio_fifo_);
    audio_fifo_ = nullptr;
  }

  fifo_frame_size_ = 0;
}