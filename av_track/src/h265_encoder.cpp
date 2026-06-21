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

H265Encoder::H265Encoder(bool debug_enabled, const std::string &codec_name)
    : debug_enabled_(debug_enabled), codec_name_(codec_name),
      encoder_context_(nullptr), codec_(nullptr) {}

H265Encoder::~H265Encoder() { close_encoder(); }

bool H265Encoder::open_encoder(int width, int height, int fps, int64_t bit_rate,
                             const std::string &profile) {
  // 重置编码帧计数器
  frame_count_ = 0;

  codec_ = avcodec_find_encoder_by_name(codec_name_.c_str());
  if (!codec_) {
    std::cerr << "Cannot find H.265 encoder: " << codec_name_ << std::endl;
    return false;
  }
  std::cout << "Using H.265 encoder: " << codec_name_ << " (" << codec_->name << ")" << std::endl;

  encoder_context_ = avcodec_alloc_context3(codec_);

  encoder_context_->width = width;
  encoder_context_->height = height;
  encoder_context_->time_base = {1, fps};
  encoder_context_->framerate = {fps, 1};
  encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P;

  // 检测是否为硬件编码器
  bool is_hw_encoder = (codec_name_ == "h264_rkmpp" || codec_name_ == "hevc_rkmpp");
  encoder_context_->thread_count = is_hw_encoder ? 0 : 1;

  // =================== 根据 profile 切换参数 ===================
  bool is_hd = (profile == "hd");

  if (is_hd) {
    // ---------- HD 高清场景 ----------
    encoder_context_->gop_size = fps * 2;       // 2秒一个关键帧
    encoder_context_->keyint_min = fps;          // 最小1秒可插入I帧
    encoder_context_->max_b_frames = 0;
    encoder_context_->has_b_frames = 0;

    if (bit_rate > 0) {
      encoder_context_->bit_rate = bit_rate;
      encoder_context_->rc_max_rate = bit_rate * 2;
      encoder_context_->rc_buffer_size = bit_rate / 4;
      av_opt_set(encoder_context_->priv_data, "preset", "medium", 0);
      av_opt_set(encoder_context_->priv_data, "tune", "film", 0);
    } else {
      int auto_bitrate = (width * height * fps) / 22;
      if (auto_bitrate < 400000) auto_bitrate = 400000;
      if (auto_bitrate > 8000000) auto_bitrate = 8000000;
      encoder_context_->bit_rate = auto_bitrate;
      encoder_context_->rc_max_rate = auto_bitrate * 2;
      encoder_context_->rc_buffer_size = auto_bitrate / 4;
      av_opt_set(encoder_context_->priv_data, "preset", "medium", 0);
      av_opt_set(encoder_context_->priv_data, "tune", "film", 0);
      av_opt_set(encoder_context_->priv_data, "crf", "24", 0);
      std::cout << "H265 Auto bitrate (HD): " << auto_bitrate / 1000 << " kbps" << std::endl;
    }

    if (is_hw_encoder) {
      av_opt_set_int(encoder_context_->priv_data, "gop_size", fps * 2, 0);
      av_opt_set_int(encoder_context_->priv_data, "keyint_min", fps, 0);
      av_opt_set_int(encoder_context_->priv_data, "rc_mode", 1, 0);
      av_opt_set_int(encoder_context_->priv_data, "rc_reenc", 0, 0);
      std::cout << "Rockchip MPP H265: GOP=" << fps * 2 << ", keyint_min=" << fps << std::endl;
    }
  } else {
    // ---------- LOWLATENCY 低延时场景（默认）----------
    encoder_context_->gop_size = fps / 2;           // 0.5秒一个关键帧
    if (encoder_context_->gop_size < 15) encoder_context_->gop_size = 15;
    encoder_context_->keyint_min = fps / 4;
    if (encoder_context_->keyint_min < 5) encoder_context_->keyint_min = 5;
    encoder_context_->max_b_frames = 0;
    encoder_context_->has_b_frames = 0;

    // Rockchip MPP 低延时专用参数
    if (is_hw_encoder) {
      av_opt_set_int(encoder_context_->priv_data, "gop_size", encoder_context_->gop_size, 0);
      av_opt_set_int(encoder_context_->priv_data, "keyint_min", encoder_context_->keyint_min, 0);
      av_opt_set_int(encoder_context_->priv_data, "rc_mode", 1, 0);   // CBR
      av_opt_set_int(encoder_context_->priv_data, "rc_reenc", 0, 0);  // 禁重编码
      std::cout << "Rockchip MPP H265 LowLatency: GOP=" << encoder_context_->gop_size
                << ", keyint_min=" << encoder_context_->keyint_min
                << ", rc_mode=CBR, rc_reenc=off" << std::endl;
    }

    // 计算最终码率
    int64_t final_bitrate = bit_rate;
    if (bit_rate <= 0) {
      final_bitrate = static_cast<int64_t>(width) * height * fps / 30;
      if (final_bitrate < 150000) final_bitrate = 150000;
      if (final_bitrate > 4000000) final_bitrate = 4000000;
    }

    encoder_context_->bit_rate = final_bitrate;
    encoder_context_->rc_max_rate = final_bitrate * 1.2;
    // ⚡ VBV 缓冲区缩小到 1 帧大小
    encoder_context_->rc_buffer_size = final_bitrate / fps;
    if (encoder_context_->rc_buffer_size < 1000) {
      encoder_context_->rc_buffer_size = 1000;
    }

    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);

    if (bit_rate <= 0) {
      av_opt_set(encoder_context_->priv_data, "crf", "28", 0);
      std::cout << "H265 Auto bitrate (LowLatency): " << final_bitrate / 1000 << " kbps" << std::endl;
    }

    std::cout << "H265 VBV buffer: " << encoder_context_->rc_buffer_size
              << " bits (~" << (encoder_context_->rc_buffer_size * 1000 / final_bitrate)
              << "ms buffering)" << std::endl;
  }

  std::cout << "Profile: " << profile
            << ", GOP=" << encoder_context_->gop_size
            << ", keyint_min=" << encoder_context_->keyint_min
            << ", bitrate=" << encoder_context_->bit_rate << " bps" << std::endl;

  int ret = avcodec_open2(encoder_context_, codec_, nullptr);
  if (ret < 0) {
    std::cerr << "Cannot open H.265 encoder: " << av_error_string(ret)
              << std::endl;
    return false;
  }

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
  if (frame && frame->pts == AV_NOPTS_VALUE) {
    frame->pts = frame_count_;
  }

  // 显式控制帧类型，强制 GOP（对 hevc_rkmpp 尤其重要）
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
  frame_count_++;

  int ret = avcodec_send_frame(encoder_context_, frame);
  if (ret < 0) return false;

  ret = avcodec_receive_packet(encoder_context_, packet);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return false;
  if (ret < 0) {
    std::cerr << "Error receiving H.265 packet: " << av_error_string(ret) << std::endl;
    return false;
  }

  if (debug_enabled_) {
    std::cout << "H265 packet size: " << packet->size
              << ", pts: " << packet->pts
              << ", keyframe: " << (packet->flags & AV_PKT_FLAG_KEY)
              << std::endl;
  }

  return true;
}
