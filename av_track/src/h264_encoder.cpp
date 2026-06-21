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
  // 重置编码帧计数器（每次重开编码器时清零）
  frame_count_ = 0;
  keyframe_count_ = 0;
  last_keyframe_pts_ = -1;

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

  // ==================== 检测是否为硬件编码器 ====================
  bool is_hw_encoder = (codec_name_ == "h264_rkmpp" || codec_name_ == "hevc_rkmpp");
  // 硬件编码器不设置 thread_count（由硬件内部管理），软编码用1-2线程
  encoder_context_->thread_count = is_hw_encoder ? 0 : 1;

  // ==================== 根据 profile 切换参数 ====================
  bool is_hd = (profile == "hd");

  if (is_hd) {
    // ---------- HD 高清场景 ----------
    // 关键帧间隔适中（平衡压缩率与恢复速度）
    encoder_context_->gop_size = fps * 2;       // 2秒一个关键帧
    encoder_context_->keyint_min = fps;          // 最小1秒可插入I帧
    encoder_context_->max_b_frames = 0;        // 仍禁用B帧（延迟敏感）
    encoder_context_->has_b_frames = 0;

    // ========== Rockchip MPP 专用优化 ==========
    if (is_hw_encoder) {
      av_opt_set_int(encoder_context_->priv_data, "gop_size", fps * 2, 0);
      av_opt_set_int(encoder_context_->priv_data, "keyint_min", fps, 0);
      av_opt_set_int(encoder_context_->priv_data, "rc_mode", 1, 0);
      av_opt_set_int(encoder_context_->priv_data, "rc_reenc", 0, 0);
      
      std::cout << "Rockchip MPP encoder: trying to set GOP=" << fps * 2 
                << ", keyint_min=" << fps << std::endl;
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
    // 低延时优化：极短 GOP + 极小 VBV 缓冲区 + 硬件编码器专用参数
    encoder_context_->gop_size = fps / 2;           // 0.5秒一个关键帧（极速恢复）
    if (encoder_context_->gop_size < 15) encoder_context_->gop_size = 15;
    encoder_context_->keyint_min = fps / 4;          // 最小0.25秒可插入I帧
    if (encoder_context_->keyint_min < 5) encoder_context_->keyint_min = 5;
    encoder_context_->max_b_frames = 0;
    encoder_context_->has_b_frames = 0;

    // ========== Rockchip MPP 低延时专用优化 ==========
    if (is_hw_encoder) {
      av_opt_set_int(encoder_context_->priv_data, "gop_size", encoder_context_->gop_size, 0);
      av_opt_set_int(encoder_context_->priv_data, "keyint_min", encoder_context_->keyint_min, 0);
      // CBR 码率控制（模式1），低延时必须用 CBR
      av_opt_set_int(encoder_context_->priv_data, "rc_mode", 1, 0);
      // 禁用重编码，减少编码延迟
      av_opt_set_int(encoder_context_->priv_data, "rc_reenc", 0, 0);
      
      std::cout << "Rockchip MPP LowLatency: GOP=" << encoder_context_->gop_size 
                << ", keyint_min=" << encoder_context_->keyint_min 
                << ", rc_mode=CBR, rc_reenc=off" << std::endl;
    }

    // 计算最终码率（bit_rate=0 表示自动计算）
    int64_t final_bitrate = bit_rate;
    if (bit_rate <= 0) {
      final_bitrate = static_cast<int64_t>(width) * height * fps / 20;
      if (final_bitrate < 200000) final_bitrate = 200000;
      if (final_bitrate > 5000000) final_bitrate = 5000000;
    }

    encoder_context_->bit_rate = final_bitrate;
    // rc_max_rate = 目标码率 × 1.2（低延时场景不需要过大波动）
    encoder_context_->rc_max_rate = final_bitrate * 1.2;
    // ⚡ 关键优化：VBV 缓冲区缩小到仅 1 帧大小
    // 原来 bit_rate/4 对应 ~250ms 缓冲，现在 bit_rate/fps 仅 ~4ms
    encoder_context_->rc_buffer_size = final_bitrate / fps;
    // 安全下限：至少 1Kbit，避免某些编码器把 0 当无限制
    if (encoder_context_->rc_buffer_size < 1000) {
      encoder_context_->rc_buffer_size = 1000;
    }

    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);

    if (bit_rate <= 0) {
      av_opt_set(encoder_context_->priv_data, "crf", "23", 0);
      std::cout << "Auto bitrate (LowLatency): " << final_bitrate / 1000 << " kbps" << std::endl;
    }

    std::cout << "VBV buffer: " << encoder_context_->rc_buffer_size 
              << " bits (~" << (encoder_context_->rc_buffer_size * 1000 / final_bitrate) 
              << "ms buffering)" << std::endl;

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
  if (frame && frame->pts == AV_NOPTS_VALUE) {
    frame->pts = frame_count_;  // 使用帧序号作为 PTS
  }

  // 显式控制帧类型，强制 GOP（对 h264_rkmpp 尤其重要）
  // 编码器内部 GOP 在 MPP 上不可靠，由我们手动管理 I/P 帧间隔
  int target_gop = (encoder_context_->gop_size > 0)
                        ? encoder_context_->gop_size : 60;
  if (frame && (frame_count_ % target_gop == 0)) {
    frame->pict_type = AV_PICTURE_TYPE_I;   // 强制 I 帧
    frame->key_frame = 1;
  } else {
    frame->pict_type = AV_PICTURE_TYPE_P;  // P 帧
    frame->key_frame = 0;
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

  frame_count_++;
  
  // 检测关键帧
  if (packet->flags & AV_PKT_FLAG_KEY) {
    keyframe_count_++;
    int64_t gap = (last_keyframe_pts_ >= 0) ? (packet->pts - last_keyframe_pts_) : 0;
    last_keyframe_pts_ = packet->pts;
    
    // 每次关键帧都输出日志
    std::cout << "🔑 KEYFRAME #" << keyframe_count_ 
              << " at pts=" << packet->pts 
              << ", gap=" << gap << " frames"
              << ", total_frames=" << frame_count_ 
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
