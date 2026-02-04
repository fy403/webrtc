#include "h265_encoder.h"
#include "debug_utils.h"
#include "encoder.h"
#include <iostream>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

extern std::string av_error_string(int errnum);

H265Encoder::H265Encoder(bool debug_enabled)
    : debug_enabled_(debug_enabled), encoder_context_(nullptr),
      codec_(nullptr) {}

H265Encoder::~H265Encoder() { close_encoder(); }

bool H265Encoder::open_encoder(int width, int height, int fps, int64_t bit_rate) {
  // 使用 libx265 软件编码
  codec_ = avcodec_find_encoder_by_name("libx265");
  if (!codec_) {
    codec_ = avcodec_find_encoder(AV_CODEC_ID_H265);
    if (!codec_) {
      std::cerr << "Cannot find H.265 encoder" << std::endl;
      return false;
    }
  }

  encoder_context_ = avcodec_alloc_context3(codec_);

  // ==================== 基础视频参数配置 ====================
  encoder_context_->width = width;        // 视频宽度
  encoder_context_->height = height;      // 视频高度
  encoder_context_->time_base = {1, fps}; // 时间基：每帧持续时间
  encoder_context_->framerate = {fps, 1}; // 帧率

  // ==================== B帧配置 ====================
  // 完全禁用B帧以减少编码延迟和提高兼容性
  encoder_context_->max_b_frames = 0; // 最大连续B帧数为0
  encoder_context_->has_b_frames = 0; // 标记流中无B帧

  // 码率控制：VBR运行较大波动
  if (bit_rate > 0) {
    encoder_context_->bit_rate = bit_rate;
    encoder_context_->rc_max_rate = bit_rate;
    encoder_context_->rc_buffer_size = bit_rate;
  }

  // 软编码配置
  encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P; // 像素格式：YUV420平面格式

  // 设置编码器参数
  av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
  av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
  av_opt_set(encoder_context_->priv_data, "crf", "28", 0); // H.265默认CRF值稍高，因为压缩效率更高

  // 设置线程用于并行编码
  encoder_context_->thread_count = 2;
  std::cout << "H265 Encoder Using " << encoder_context_->thread_count
            << " threads" << std::endl;

  std::cout << "Encoder configured with GOP size: " << encoder_context_->gop_size
            << std::endl;

  int ret = avcodec_open2(encoder_context_, codec_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot open H.265 encoder: " << av_error_string(ret)
              << std::endl;
    return false;
  }

  // 检查编码器是否支持全局头
  std::cout << "Encoder supports global header: "
            << (encoder_context_->flags & AV_CODEC_FLAG_GLOBAL_HEADER)
            << std::endl;

  return true;
}

void H265Encoder::close_encoder() {
  if (encoder_context_) {
    avcodec_free_context(&encoder_context_);
    encoder_context_ = nullptr;
  }
}

bool H265Encoder::encode_frame(AVFrame *frame, AVPacket *packet) {
  // Ensure frame has timestamp
  static int64_t pts = 0;
  if (frame && frame->pts == AV_NOPTS_VALUE) {
    frame->pts = pts++;
  }

  int ret = avcodec_send_frame(encoder_context_, frame);

  if (ret < 0) {
    return false;
  }

  ret = avcodec_receive_packet(encoder_context_, packet);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return false;
  } else if (ret < 0) {
    std::cerr << "Error receiving packet from encoder: " << av_error_string(ret)
              << std::endl;
    return false;
  }

  if (debug_enabled_) {
    std::cout << "H265 packet size: " << packet->size
              << ", packet pts: " << packet->pts
              << ", keyframe: " << (packet->flags & AV_PKT_FLAG_KEY)
              << std::endl;
  }

  return true;
}
