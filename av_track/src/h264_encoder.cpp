#include "h264_encoder.h"
#include "debug_utils.h"
#include "encoder.h"
#include <iostream>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

extern std::string av_error_string(int errnum);

H264Encoder::H264Encoder(bool debug_enabled, const std::string &codec_name)
    : debug_enabled_(debug_enabled), codec_name_(codec_name),
      encoder_context_(nullptr), codec_(nullptr) {}

H264Encoder::~H264Encoder() { close_encoder(); }

bool H264Encoder::open_encoder(int width, int height, int fps, int64_t bit_rate) {
  // 使用指定的编码器名称（支持 libx264, h264_rkmpp 等）
  codec_ = avcodec_find_encoder_by_name(codec_name_.c_str());
  if (!codec_) {
    std::cerr << "Cannot find H.264 encoder: " << codec_name_ << std::endl;
    return false;
  }
  std::cout << "Using H.264 encoder: " << codec_name_ << " (" << codec_->name << ")" << std::endl;

  encoder_context_ = avcodec_alloc_context3(codec_);

  // ==================== 基础视频参数配置 ====================
  encoder_context_->width = width;        // 视频宽度
  encoder_context_->height = height;      // 视频高度
  encoder_context_->time_base = {1, fps}; // 时间基：每帧持续时间
  encoder_context_->framerate = {fps, 1}; // 帧率

  // ==================== 关键帧/GOP 配置（优化丢帧率）====================
  // GOP = fps * 2：每2秒一个关键帧，平衡压缩率和随机访问能力
  encoder_context_->gop_size = fps * 2;
  // keyint_min = fps：最小关键帧间隔1秒
  encoder_context_->keyint_min = fps;

  // ==================== B帧配置 ====================
  // 完全禁用B帧以减少编码延迟和提高兼容性
  encoder_context_->max_b_frames = 0; // 最大连续B帧数为0
  encoder_context_->has_b_frames = 0; // 标记流中无B帧

  // ==================== 码率控制 ====================
  // 优先使用指定码率；若未指定则根据分辨率自动计算
  if (bit_rate > 0) {
    // 用户指定了码率
    encoder_context_->bit_rate = bit_rate;
    encoder_context_->rc_max_rate = bit_rate * 2;     // 最大码率为目标值的2倍
    encoder_context_->rc_buffer_size = bit_rate / 4;  // 缓冲区大小
    // 使用 ABR 模式替代 CRF，更稳定的码率输出
    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);
  } else {
    // 未指定码率 → 自动计算合理码率 + CRF 模式
    // 经验公式：640x480@30fps ≈ 800kbps, 1280x720@30fps ≈ 2000kbps
    int auto_bitrate = (width * height * fps) / 20;  // 自适应码率计算
    if (auto_bitrate < 200000) auto_bitrate = 200000;  // 最低 200kbps
    if (auto_bitrate > 5000000) auto_bitrate = 5000000;  // 最高 5Mbps
    encoder_context_->bit_rate = auto_bitrate;
    encoder_context_->rc_max_rate = auto_bitrate * 2;
    encoder_context_->rc_buffer_size = auto_bitrate / 4;

    // CRF 模式用于自适应质量
    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(encoder_context_->priv_data, "crf", "23", 0);
    av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);

    std::cout << "Auto bitrate calculated: " << auto_bitrate / 1000 << " kbps" << std::endl;
  }
  encoder_context_->level = 31;

  // 软编码配置
  encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P; // 像素格式：YUV420平面格式

  // 设置线程用于并行编码
  encoder_context_->thread_count = 2;
  std::cout << "H264 Encoder Using " << encoder_context_->thread_count
            << " threads" << std::endl;

  std::cout << "Encoder configured with GOP=" << encoder_context_->gop_size
            << ", keyint_min=" << encoder_context_->keyint_min
            << ", bitrate=" << encoder_context_->bit_rate << " bps" << std::endl;

  int ret = avcodec_open2(encoder_context_, codec_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot open H.264 encoder: " << av_error_string(ret)
              << std::endl;
    return false;
  }

  // 检查编码器是否支持全局头
  std::cout << "Encoder supports global header: "
            << (encoder_context_->flags & AV_CODEC_FLAG_GLOBAL_HEADER)
            << std::endl;

  return true;
}

void H264Encoder::close_encoder() {
  if (encoder_context_) {
    avcodec_free_context(&encoder_context_);
    encoder_context_ = nullptr;
  }
}

bool H264Encoder::encode_frame(AVFrame *frame, AVPacket *packet) {
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
    std::cout << "packet size: " << packet->size
              << ", packet pts: " << packet->pts
              << ", keyframe: " << (packet->flags & AV_PKT_FLAG_KEY)
              << std::endl;
    // 分析 NALU
    DebugUtils::analyze_nal_units(packet);
  }

  return true;
}