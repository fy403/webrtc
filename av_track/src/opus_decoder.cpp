#include "opus_decoder.h"
#include <iomanip> // Needed for std::setw and std::setfill
#include <iostream>

extern std::string av_error_string(int errnum);

OpusDecoder::OpusDecoder() : decoder_context_(nullptr), codec_(nullptr) {}

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
  return true;
}

bool OpusDecoder::decode_packet(AVPacket *packet, AVFrame *frame) {
  if (!decoder_context_) {
    std::cerr << "Decoder not initialized" << std::endl;
    return false;
  }

  // Validate packet
  if (!packet || !frame) {
    std::cerr << "Invalid packet or frame" << std::endl;
    return false;
  }

  if (!packet->data || packet->size <= 0) {
    std::cerr << "Invalid packet data or size: " << packet->size << std::endl;
    return false;
  }

  std::cout << "Sending packet to decoder, size: " << packet->size << std::endl;

  // Send packet to decoder
  int ret = avcodec_send_packet(decoder_context_, packet);
  if (ret < 0) {
    if (ret != AVERROR(EAGAIN)) {
      // Try to provide more context about the error
      char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
      av_strerror(ret, errBuf, sizeof(errBuf));
      std::cerr << "Error sending packet for decoding: " << errBuf << " ("
                << ret << ")" << std::endl;

      // Print first few bytes of the packet for debugging
      std::cerr << "Packet data (first 16 bytes): ";
      for (int i = 0; i < std::min(packet->size, 16); ++i) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)packet->data[i] << " ";
      }
      std::cerr << std::dec << std::endl;
    }
    return false;
  }

  // Receive decoded frame
  ret = avcodec_receive_frame(decoder_context_, frame);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    std::cout << "Decoder needs more data or reached EOF" << std::endl;
    return false;
  } else if (ret < 0) {
    std::cerr << "Error during decoding: " << av_error_string(ret) << std::endl;
    return false;
  }

  std::cout << "Packet decoded successfully, samples: " << frame->nb_samples
            << std::endl;

  return true;
}

void OpusDecoder::close_decoder() {
  if (decoder_context_) {
    avcodec_free_context(&decoder_context_);
    decoder_context_ = nullptr;
  }
}