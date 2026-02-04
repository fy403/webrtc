#ifndef VIDEO_CAPTURER_H
#define VIDEO_CAPTURER_H

#include "capture.h"
#include <memory>
#include <string>

// Forward declarations
class Encoder;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace rtc {
class Track;
}

class VideoCapturer : public Capture {
public:
  VideoCapturer(const std::string &device = "/dev/video1",
                bool debug_enabled = false,
                const std::string &resolution = "640x480", int framerate = 30,
                const std::string &video_format = "mjpeg",
                size_t decode_queue_capacity = 512,
                size_t encode_queue_capacity = 512,
                size_t send_queue_capacity = 512);
  ~VideoCapturer();
  bool start() override;
  void stop() override;
  void reconfigure(const std::string &resolution, int fps, int bitrate, const std::string &format);
  void set_video_codec(const std::string &codec); // 设置视频编码器类型 (h264 or h265)
  std::string get_video_codec() const { return video_codec_; } // 获取当前视频编码器类型

private:
  void capture_loop() override;
  void decode_loop() override;
  void encode_loop() override;
  void send_loop() override;

  // Frame pool for scaled YUV420P frames to reduce frequent alloc/free
  AVFrame *acquire_scaled_frame();
  void release_scaled_frame(AVFrame *frame);
  void clear_frame_pool();

  bool is_udp_stream_ = false;  // 是否为UDP流模式
  std::string device_;
  std::string resolution_;
  int framerate_;
  std::string video_format_;
  std::string video_codec_ = "h264"; // 视频编码器类型: h264 or h265
  AVFormatContext *format_context_ = nullptr;
  AVCodecContext *codec_context_ = nullptr;
  SwsContext *sws_context_ = nullptr;
  int video_stream_index_ = -1;

  // Cached encoder output parameters for sws_context_ and frame pool
  int encoder_out_width_ = 0;
  int encoder_out_height_ = 0;
  AVPixelFormat encoder_out_pix_fmt_ = AV_PIX_FMT_YUV420P;

  // A small pool of reusable scaled frames
  std::vector<AVFrame *> scaled_frame_pool_;
  std::mutex frame_pool_mutex_;

  std::shared_ptr<rtc::Track> track_;
};

#endif // VIDEO_CAPTURER_H