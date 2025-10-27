#ifndef CAPTURE_H
#define CAPTURE_H

#include "safe_queue.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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

class Capture {
public:
  Capture(bool debug_enabled = false, size_t encode_queue_capacity = 512,
          size_t send_queue_capacity = 512);
  virtual ~Capture();

  bool is_running();
  virtual bool start() = 0;
  virtual void stop();
  virtual void pause_capture();
  virtual void resume_capture();

  using TrackCallback = std::function<void(const std::byte *data, size_t size)>;
  void set_track_callback(TrackCallback callback);

protected:
  virtual void capture_loop() = 0;
  virtual void encode_loop() = 0;
  virtual void send_loop() = 0;

  bool debug_enabled_;
  std::atomic<bool> is_running_ = false;
  std::atomic<bool> is_paused_ = true;
  std::thread capture_thread_;
  std::thread encode_thread_;
  std::thread send_thread_;
  TrackCallback track_callback_;

  std::unique_ptr<Encoder> encoder_;

  std::shared_ptr<rtc::Track> track_;

  // 添加互斥锁和条件变量用于同步回调函数的设置
  std::mutex callback_mutex_;
  std::condition_variable callback_cv_;

  // Queues for async processing
  SafeQueue<AVFrame *> encode_queue_;
  SafeQueue<AVPacket *> send_queue_;
};

#endif // CAPTURE_H