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
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
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
  bool is_fake_camera_ = false; // 是否为模拟摄像头模式(lavfi)
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

  // FPS 滤镜图：当摄像头实际帧率与目标帧率不一致时，
  // 通过 vf=fps=N 实现帧补全（低→高）或丢帧（高→低）
  AVFilterGraph *fps_filter_graph_ = nullptr;
  AVFilterContext *fps_buffer_src_ = nullptr;
  AVFilterContext *fps_buffer_sink_ = nullptr;
  int target_fps_;  // 目标输出帧率

  bool init_fps_filter(int width, int height, int fps);
  void cleanup_fps_filter();

  std::shared_ptr<rtc::Track> track_;
};

#endif // VIDEO_CAPTURER_H