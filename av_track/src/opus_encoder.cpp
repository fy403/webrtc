#include "opus_encoder.h"
#include "debug_utils.h"
#include <cmath>
#include <iostream>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

extern std::string av_error_string(int errnum);

OpusEncoder::OpusEncoder(bool debug_enabled)
    : debug_enabled_(debug_enabled), codec_(nullptr), encoder_context_(nullptr),
      frame_count_(0), audio_fifo_(nullptr), fifo_frame_size_(0) {}

OpusEncoder::~OpusEncoder() { close_encoder(); }

bool OpusEncoder::open_encoder(int sample_rate, int channels,
                               AVSampleFormat sample_fmt) {
  codec_ = avcodec_find_encoder(AV_CODEC_ID_OPUS);
  if (!codec_) {
    std::cerr << "Cannot find Opus encoder" << std::endl;
    return false;
  }

  encoder_context_ = avcodec_alloc_context3(codec_);
  if (!encoder_context_) {
    std::cerr << "Cannot allocate Opus encoder context" << std::endl;
    return false;
  }

  // Opus 编码器标准配置
  encoder_context_->sample_rate = sample_rate; // Opus 标准采样率
  encoder_context_->channels = channels;
  // encoder_context_->channel_layout = AV_CH_LAYOUT_MONO;
  encoder_context_->channel_layout = av_get_default_channel_layout(channels);

  // Opus 编码器支持的格式
  encoder_context_->sample_fmt = sample_fmt; // Opus 使用 S16 格式

  // 设置 Opus 编码器参数以增加帧大小
  av_opt_set(encoder_context_->priv_data, "application", "audio", 0);
  av_opt_set_int(encoder_context_->priv_data, "frame_duration", 20,
                 0); // 设置帧持续时间为20毫秒
  
  // 增加编码器缓冲以避免xrun
  av_opt_set_int(encoder_context_->priv_data, "packet_loss", 10, 0);
  av_opt_set_int(encoder_context_->priv_data, "fec", 1, 0);

  int ret = avcodec_open2(encoder_context_, codec_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot open Opus encoder: " << av_error_string(ret)
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
  fifo_frame_size_ = encoder_context_->frame_size;

  if (debug_enabled_) {
    std::cout << "Opus encoder initialized: " << encoder_context_->sample_rate
              << "Hz, " << encoder_context_->channels << "ch, "
              << "frame_size: " << encoder_context_->frame_size
              << ", bitrate: " << encoder_context_->bit_rate << std::endl;
  }

  return true;
}

bool OpusEncoder::write_to_fifo(AVFrame *frame) {
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

  if (debug_enabled_) {
    std::cout << "write ok, audio_fifo.size = "
              << av_audio_fifo_size(audio_fifo_) << std::endl;
  }

  return true;
}

int OpusEncoder::read_from_fifo(AVFrame *frame) {
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

  //  按照Opus编码器的要求，将帧的采样数设置为帧大小
  frame->nb_samples = fifo_frame_size_;
  frame->channels = encoder_context_->channels;
  frame->channel_layout = encoder_context_->channel_layout;
  frame->sample_rate = encoder_context_->sample_rate;
  frame->format = encoder_context_->sample_fmt;

  if (debug_enabled_) {
    std::cout << "Read " << read_samples << " samples from FIFO" << std::endl;
    std::cout << "audio_fifo.size = " << av_audio_fifo_size(audio_fifo_)
              << std::endl;
  }

  return read_samples;
}

bool OpusEncoder::encode_frame(AVFrame *frame, AVPacket *packet) {
  if (!encoder_context_) {
    std::cerr << "Encoder not initialized" << std::endl;
    return false;
  }

  // 首先将输入帧写入 FIFO
  if (frame != nullptr) {
    if (!write_to_fifo(frame)) {
      std::cerr << "Failed to write frame to FIFO" << std::endl;
      return false;
    }
  }

  // 创建一个临时帧用于从 FIFO 读取数据
  AVFrame *fifo_frame = av_frame_alloc();
  if (!fifo_frame) {
    std::cerr << "Could not allocate FIFO frame" << std::endl;
    return false;
  }

  // 设置 FIFO 帧的属性
  fifo_frame->nb_samples = fifo_frame_size_;
  fifo_frame->channels = encoder_context_->channels;
  fifo_frame->channel_layout = encoder_context_->channel_layout;
  fifo_frame->sample_rate = encoder_context_->sample_rate;
  fifo_frame->format = encoder_context_->sample_fmt;

  // 为帧分配缓冲区
  int ret = av_frame_get_buffer(fifo_frame, 0);
  if (ret < 0) {
    std::cerr << "Could not allocate FIFO frame buffers: "
              << av_error_string(ret) << std::endl;
    av_frame_free(&fifo_frame);
    return false;
  }

  // 从 FIFO 读取数据
  int read_samples = read_from_fifo(fifo_frame);
  if (read_samples <= 0) {
    // FIFO 中没有足够的数据
    av_frame_free(&fifo_frame);
    return false;
  }

  // 设置时间戳
  fifo_frame->pts = frame_count_ * fifo_frame_size_;

  // 发送帧到编码器
  ret = avcodec_send_frame(encoder_context_, fifo_frame);

  if (ret == AVERROR(EINVAL)) {
    std::cerr << "Encoder received invalid frame data" << std::endl;
    av_frame_free(&fifo_frame);
    return false;
  } else if (ret < 0 && ret != AVERROR_EOF) {
    std::cerr << "Error sending frame to Opus encoder: " << av_error_string(ret)
              << std::endl;
    av_frame_free(&fifo_frame);
    return false;
  }

  // 接收编码后的包
  ret = avcodec_receive_packet(encoder_context_, packet);
  if (ret == AVERROR(EAGAIN)) {
    av_frame_free(&fifo_frame);
    return false; // 需要更多输入
  } else if (ret == AVERROR_EOF) {
    av_frame_free(&fifo_frame);
    return false; // 编码器已刷新
  } else if (ret < 0) {
    std::cerr << "Error receiving packet from Opus encoder: "
              << av_error_string(ret) << std::endl;
    av_frame_free(&fifo_frame);
    return false;
  }

  // 设置包的时间戳
  packet->pts = fifo_frame->pts;
  packet->dts = packet->pts;
  frame_count_++;
  if (debug_enabled_) {
    std::cout << "Package PTS: " << packet->pts << ", DTS: " << packet->dts
              << ", Size: " << packet->size << " bytes" << std::endl;
  }

  av_frame_free(&fifo_frame);
  return true;
}

void OpusEncoder::close_encoder() {
  if (encoder_context_) {
    avcodec_free_context(&encoder_context_);
    encoder_context_ = nullptr;
  }

  if (audio_fifo_) {
    av_audio_fifo_free(audio_fifo_);
    audio_fifo_ = nullptr;
  }

  frame_count_ = 0;
}