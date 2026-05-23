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
  encoder_context_->thread_count = 2;

  // =================== 根据 profile 切换参数 ===================
  bool is_hd = (profile == "hd");

  if (is_hd) {
    // ---------- HD 高清场景 ----------
    encoder_context_->gop_size = fps * 4;
    encoder_context_->keyint_min = fps * 2;
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
    } else {
      int auto_bitrate = (width * height * fps) / 30;
      if (auto_bitrate < 150000) auto_bitrate = 150000;
      if (auto_bitrate > 4000000) auto_bitrate = 4000000;
      encoder_context_->bit_rate = auto_bitrate;
      encoder_context_->rc_max_rate = auto_bitrate * 2;
      encoder_context_->rc_buffer_size = auto_bitrate / 4;
      av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
      av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
      av_opt_set(encoder_context_->priv_data, "crf", "28", 0);
      std::cout << "H265 Auto bitrate (LowLatency): " << auto_bitrate / 1000 << " kbps" << std::endl;
    }
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
  static int64_t pts = 0;
  if (frame && frame->pts == AV_NOPTS_VALUE) {
    frame->pts = pts++;
  }

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
