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

bool H264Encoder::open_encoder(int width, int height, int fps, int64_t bit_rate,
                             const std::string &profile) {
  // 使用指定的编码器名称（支持 libx264, h264_rkmpp 等）
  codec_ = avcodec_find_encoder_by_name(codec_name_.c_str());
  if (!codec_) {
    std::cerr << "Cannot find H.264 encoder: " << codec_name_ << std::endl;
    return false;
  }
  std::cout << "Using H.264 encoder: " << codec_name_ << " (" << codec_->name << ")" << std::endl;

  encoder_context_ = avcodec_alloc_context3(codec_);

  // ==================== 基础视频参数配置 ====================
  encoder_context_->width = width;
  encoder_context_->height = height;
  encoder_context_->time_base = {1, fps};
  encoder_context_->framerate = {fps, 1};
  encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P;
  encoder_context_->thread_count = 2;

  // ==================== 根据 profile 切换参数 ====================
  bool is_hd = (profile == "hd");

  if (is_hd) {
    // ---------- HD 高清场景 ----------
    // 关键帧间隔加长（降低 I 帧占比，提升压缩率）
    encoder_context_->gop_size = fps * 4;       // 4秒一个关键帧
    encoder_context_->keyint_min = fps * 2;    // 最小间隔2秒
    encoder_context_->max_b_frames = 0;        // 仍禁用B帧（延迟敏感）
    encoder_context_->has_b_frames = 0;

    // ========== Rockchip MPP 专用优化 ==========
    // h264_rkmpp 编码器需要特殊参数设置 GOP
    if (codec_name_ == "h264_rkmpp" || codec_name_ == "hevc_rkmpp") {
      // MPP 编码器：设置关键帧间隔（单位：帧数）
      // rc_reenc = 0: 禁用重新编码（降低延迟）
      // rc_mode = 1: CBR 模式（稳定码率）
      // gop_size: 关键帧间隔
      // 注意：Rockchip MPP 可能使用不同的参数名
      av_opt_set_int(encoder_context_->priv_data, "gop_size", fps * 4, 0);
      av_opt_set_int(encoder_context_->priv_data, "keyint_min", fps * 2, 0);
      av_opt_set_int(encoder_context_->priv_data, "rc_mode", 1, 0);  // CBR
      av_opt_set_int(encoder_context_->priv_data, "rc_reenc", 0, 0); // 禁用重编码
      
      // 尝试设置其他可能的参数名（Rockchip MPP 可能不支持标准参数名）
      av_opt_set_int(encoder_context_->priv_data, "gop", fps * 4, 0);
      av_opt_set_int(encoder_context_->priv_data, "i_interval", fps * 4, 0);
      
      std::cout << "Rockchip MPP encoder: trying to set GOP=" << fps * 4 
                << ", keyint_min=" << fps * 2 << std::endl;
      std::cout << "Note: Rockchip MPP may not support standard GOP parameters" << std::endl;
    }

    if (bit_rate > 0) {
      encoder_context_->bit_rate = bit_rate;
      encoder_context_->rc_max_rate = bit_rate * 2;
      encoder_context_->rc_buffer_size = bit_rate / 4;
      // HD 场景使用较慢 preset 以获得更好质量
      av_opt_set(encoder_context_->priv_data, "preset", "medium", 0);
      av_opt_set(encoder_context_->priv_data, "tune", "film", 0);
      av_opt_set(encoder_context_->priv_data, "profile", "main", 0);
    } else {
      int auto_bitrate = (width * height * fps) / 15;  // HD 码率稍高
      if (auto_bitrate < 500000) auto_bitrate = 500000;
      if (auto_bitrate > 10000000) auto_bitrate = 10000000;
      encoder_context_->bit_rate = auto_bitrate;
      encoder_context_->rc_max_rate = auto_bitrate * 2;
      encoder_context_->rc_buffer_size = auto_bitrate / 4;
      av_opt_set(encoder_context_->priv_data, "preset", "veryslow", 0);
      av_opt_set(encoder_context_->priv_data, "tune", "film", 0);
      av_opt_set(encoder_context_->priv_data, "crf", "16", 0);
      av_opt_set(encoder_context_->priv_data, "profile", "main", 0);
      std::cout << "Auto bitrate (HD): " << auto_bitrate / 1000 << " kbps" << std::endl;
    }
    encoder_context_->level = 42;
  } else {
    // ---------- LOWLATENCY 低延时场景（默认）----------
    encoder_context_->gop_size = fps * 2;
    encoder_context_->keyint_min = fps;
    encoder_context_->max_b_frames = 0;
    encoder_context_->has_b_frames = 0;

    if (bit_rate > 0) {
      encoder_context_->bit_rate = bit_rate;
      encoder_context_->rc_max_rate = bit_rate * 2;
      encoder_context_->rc_buffer_size = bit_rate / 4;
      av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
      av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
      av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);
    } else {
      int auto_bitrate = (width * height * fps) / 20;
      if (auto_bitrate < 200000) auto_bitrate = 200000;
      if (auto_bitrate > 5000000) auto_bitrate = 5000000;
      encoder_context_->bit_rate = auto_bitrate;
      encoder_context_->rc_max_rate = auto_bitrate * 2;
      encoder_context_->rc_buffer_size = auto_bitrate / 4;
      av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
      av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
      av_opt_set(encoder_context_->priv_data, "crf", "23", 0);
      av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);
      std::cout << "Auto bitrate (LowLatency): " << auto_bitrate / 1000 << " kbps" << std::endl;
    }
    encoder_context_->level = 31;
  }

  std::cout << "Profile: " << profile
            << ", GOP=" << encoder_context_->gop_size
            << ", keyint_min=" << encoder_context_->keyint_min
            << ", bitrate=" << encoder_context_->bit_rate << " bps" << std::endl;

  int ret = avcodec_open2(encoder_context_, codec_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot open H.264 encoder: " << av_error_string(ret)
              << std::endl;
    return false;
  }

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
  static int64_t frame_count = 0;
  static int64_t keyframe_count = 0;
  static int64_t last_keyframe_pts = -1;
  
  if (frame && frame->pts == AV_NOPTS_VALUE) {
    frame->pts = pts++;
  }

  // 确保时间戳连续递增（避免时间戳跳跃导致接收端解码问题）
  static int64_t last_pts = -1;
  if (frame && last_pts >= 0 && frame->pts > last_pts + 2) {
    // 如果时间戳跳跃超过2帧，警告但不修正（避免破坏时序）
    std::cerr << "⚠️ PTS jump detected: last=" << last_pts 
              << ", current=" << frame->pts << std::endl;
  }
  if (frame) {
    last_pts = frame->pts;
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

  frame_count++;
  
  // 检测关键帧
  if (packet->flags & AV_PKT_FLAG_KEY) {
    keyframe_count++;
    int64_t gap = (last_keyframe_pts >= 0) ? (packet->pts - last_keyframe_pts) : 0;
    last_keyframe_pts = packet->pts;
    
    // 每次关键帧都输出日志
    std::cout << "🔑 KEYFRAME #" << keyframe_count 
              << " at pts=" << packet->pts 
              << ", gap=" << gap << " frames"
              << ", total_frames=" << frame_count 
              << std::endl;
    
    // 如果间隔太小，警告
    if (gap > 0 && gap < encoder_context_->gop_size / 2) {
      std::cerr << "⚠️ WARNING: Keyframe gap too small! Expected GOP=" 
                << encoder_context_->gop_size << ", actual gap=" << gap << std::endl;
    }
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
