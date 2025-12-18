#include "h264_encoder.h"
#include "debug_utils.h"
#include "encoder.h"
#include <iostream>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
}

extern std::string av_error_string(int errnum);

H264Encoder::H264Encoder(bool debug_enabled)
    : debug_enabled_(debug_enabled), encoder_context_(nullptr),
      codec_(nullptr) {}

H264Encoder::~H264Encoder() { close_encoder(); }

bool H264Encoder::open_encoder(int width, int height, int fps) {
  // 在 video_capturer.cpp 的 start() 函数中修改编码器配置
  // 优先尝试 VAAPI 硬件编码，失败则回退到 libx264 软编码

  use_vaapi_ = false;

  // =============== 优先尝试 VAAPI 硬件编码 ===============
  AVHWDeviceType hw_type = av_hwdevice_find_type_by_name("vaapi");
  if (hw_type != AV_HWDEVICE_TYPE_NONE) {
    std::cout << "Try to use VAAPI hardware encoder" << std::endl;
    // 这里默认使用 /dev/dri/renderD128，如有需要可改成配置参数
    int ret = av_hwdevice_ctx_create(&hw_device_ctx_, hw_type,
                                     "/dev/dri/renderD128", nullptr, 0);
    if (ret >= 0) {
      codec_ = avcodec_find_encoder_by_name("h264_vaapi");
      if (codec_) {
        use_vaapi_ = true;
        std::cout << "Using VAAPI encoder: h264_vaapi" << std::endl;
      } else {
        std::cerr << "Cannot find h264_vaapi encoder, fallback to software"
                  << std::endl;
        av_buffer_unref(&hw_device_ctx_);
        hw_device_ctx_ = nullptr;
      }
    } else {
      std::cerr << "Create VAAPI device failed: " << av_error_string(ret)
                << ", fallback to software" << std::endl;
      hw_device_ctx_ = nullptr;
    }
  }

  // =============== 软件编码回退（libx264 或内置 H.264） ===============
  if (!use_vaapi_) {
    codec_ = avcodec_find_encoder_by_name("libx264");
    if (!codec_) {
      codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
      if (!codec_) {
        std::cerr << "Cannot find H.264 encoder" << std::endl;
        return false;
      }
    }
  }

  encoder_context_ = avcodec_alloc_context3(codec_);

  // ==================== 基础视频参数配置 ====================
  encoder_context_->width = width;        // 视频宽度
  encoder_context_->height = height;      // 视频高度
  encoder_context_->time_base = {1, fps}; // 时间基：每帧持续时间
  encoder_context_->framerate = {fps, 1}; // 帧率

  // 关键帧设置（优化）
  encoder_context_->gop_size = fps; // GOP等于帧率（1秒一个关键帧）
  // 最小关键帧间隔改为半秒，提高随机访问性
  encoder_context_->keyint_min = fps / 2;

  // ==================== B帧配置 ====================
  // 完全禁用B帧以减少编码延迟和提高兼容性
  encoder_context_->max_b_frames = 0; // 最大连续B帧数为0
  encoder_context_->has_b_frames = 0; // 标记流中无B帧

  // 码率控制：VBR运行较大波动
//  encoder_context_->bit_rate = 1200000;
//  encoder_context_->rc_max_rate = 1200000;
//  encoder_context_->rc_buffer_size = 1200000;

  if (use_vaapi_) {
    // =============== VAAPI 硬件编码相关配置 ===============
    encoder_context_->pix_fmt = AV_PIX_FMT_VAAPI;

    // 将硬件设备绑定到编码器上下文
    encoder_context_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);

    // 创建硬件帧上下文，指定输入为 YUV420P，输出为 VAAPI 表面
    hw_frames_ref_ = av_hwframe_ctx_alloc(encoder_context_->hw_device_ctx);
    if (!hw_frames_ref_) {
      std::cerr << "Cannot allocate VAAPI frame context, fallback to software"
                << std::endl;
      av_buffer_unref(&hw_device_ctx_);
      hw_device_ctx_ = nullptr;
      use_vaapi_ = false;
    } else {
      AVHWFramesContext *frames_ctx =
          reinterpret_cast<AVHWFramesContext *>(hw_frames_ref_->data);
      frames_ctx->format = AV_PIX_FMT_VAAPI;
      frames_ctx->sw_format = AV_PIX_FMT_YUV420P;
      frames_ctx->width = width;
      frames_ctx->height = height;
      frames_ctx->initial_pool_size = 20;

      int ret = av_hwframe_ctx_init(hw_frames_ref_);
      if (ret < 0) {
        std::cerr << "Cannot init VAAPI frame context: " << av_error_string(ret)
                  << ", fallback to software" << std::endl;
        av_buffer_unref(&hw_frames_ref_);
        hw_frames_ref_ = nullptr;
        av_buffer_unref(&hw_device_ctx_);
        hw_device_ctx_ = nullptr;
        use_vaapi_ = false;
      } else {
        encoder_context_->hw_frames_ctx = av_buffer_ref(hw_frames_ref_);
      }
    }
  }

  if (!use_vaapi_) {
    // =============== 软编码配置（保持原有行为） ===============
    encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P; // 像素格式：YUV420平面格式

    // 设置编码器参数
    av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(encoder_context_->priv_data, "crf", "23", 0);
    av_opt_set(encoder_context_->priv_data, "profile", "baseline", 0);
    encoder_context_->level = 31;

    // 设置线程用于并行编码
    encoder_context_->thread_count = 2;
    std::cout << "H264 Encoder Using " << encoder_context_->thread_count
              << " threads" << std::endl;
  } else {
    // 对于 VAAPI，一般由驱动和硬件内部控制并行度，这里不强制设置线程数
    std::cout << "H264 Encoder Using VAAPI hardware acceleration" << std::endl;
  }

  std::cout << "Encoder configured with GOP size: " << encoder_context_->gop_size
            << std::endl;

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
  if (hw_frames_ref_) {
    av_buffer_unref(&hw_frames_ref_);
    hw_frames_ref_ = nullptr;
  }
  if (hw_device_ctx_) {
    av_buffer_unref(&hw_device_ctx_);
    hw_device_ctx_ = nullptr;
  }
}

bool H264Encoder::encode_frame(AVFrame *frame, AVPacket *packet) {
  // Ensure frame has timestamp
  static int64_t pts = 0;
  if (frame && frame->pts == AV_NOPTS_VALUE) {
    frame->pts = pts++;
  }

  AVFrame *send_frame = frame;
  AVFrame *hw_frame = nullptr;

  if (use_vaapi_ && frame) {
    // 将 CPU 上的 YUV420P 帧上传到 VAAPI 硬件帧
    hw_frame = av_frame_alloc();
    if (!hw_frame) {
      std::cerr << "Failed to allocate VAAPI frame" << std::endl;
      return false;
    }

    hw_frame->format = AV_PIX_FMT_VAAPI;
    hw_frame->width = frame->width;
    hw_frame->height = frame->height;

    int ret = av_hwframe_get_buffer(hw_frames_ref_, hw_frame, 0);
    if (ret < 0) {
      std::cerr << "Failed to get VAAPI frame buffer: " << av_error_string(ret)
                << std::endl;
      av_frame_free(&hw_frame);
      return false;
    }

    ret = av_hwframe_transfer_data(hw_frame, frame, 0);
    if (ret < 0) {
      std::cerr << "Failed to transfer data to VAAPI frame: "
                << av_error_string(ret) << std::endl;
      av_frame_free(&hw_frame);
      return false;
    }

    hw_frame->pts = frame->pts;
    send_frame = hw_frame;
  }

  int ret = avcodec_send_frame(encoder_context_, send_frame);

  if (hw_frame) {
    av_frame_free(&hw_frame);
  }

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