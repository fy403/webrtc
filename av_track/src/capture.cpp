#include "capture.h"
#include "debug_utils.h"
#include "encoder.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <random>
#include <sstream>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/sysinfo.h>
#endif

#include "rtc/rtc.hpp"

Capture::Capture(bool debug_enabled, size_t decode_queue_capacity,
                 size_t encode_queue_capacity, size_t send_queue_capacity)
    : debug_enabled_(debug_enabled), is_running_(false), is_paused_(false),
      decode_queue_(decode_queue_capacity), encode_queue_(encode_queue_capacity), 
      send_queue_(send_queue_capacity) {
  avdevice_register_all();
  decode_queue_.set_deleter([](AVPacket *packet) { av_packet_free(&packet); });
  encode_queue_.set_deleter([](AVFrame *frame) { av_frame_free(&frame); });
  send_queue_.set_deleter([](AVPacket *packet) { av_packet_free(&packet); });
}

Capture::~Capture() { stop(); }

bool Capture::is_running() { 
  return is_running_.load(); 
}

void Capture::set_track_callback(TrackCallback callback) {
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    track_callback_ = std::move(callback);
  }
  callback_cv_.notify_one();
}

void Capture::stop() {
  // 先标记为停止运行
  is_running_ = false;
  is_paused_ = false;

  // 通知所有等待的线程（如 capture_loop 中的条件等待）
  callback_cv_.notify_all();

  // 停止并清空各个队列，唤醒其中阻塞的线程
  decode_queue_.stop();
  encode_queue_.stop();
  send_queue_.stop();

  // 等待所有线程完成
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }

  if (decode_thread_.joinable()) {
    decode_thread_.join();
  }

  if (encode_thread_.joinable()) {
    encode_thread_.join();
  }

  if (send_thread_.joinable()) {
    send_thread_.join();
  }

  // Encoder context is managed by the Encoder class
  if (encoder_) {
    encoder_->close_encoder();
  }
}

void Capture::pause_capture() {
  is_paused_ = true;

  // 清空队列
  decode_queue_.clear();
  encode_queue_.clear();
  send_queue_.clear();

  std::cout << "Capture paused and queues cleared!!!" << std::endl;
}

void Capture::resume_capture() {
  is_paused_ = false;
  // 通知等待的线程，采集已恢复
  callback_cv_.notify_all();
  std::cout << "Capture resumed!!!" << std::endl;
}

int Capture::get_cpu_count() {
#ifdef __linux__
  return get_nprocs();
#else
  return 0; // 非Linux系统不支持CPU绑定
#endif
}

void Capture::bind_thread_to_cpu(std::thread& thread, int cpu_id) {
#ifdef __linux__
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu_id, &cpuset);
  
  pthread_t thread_handle = thread.native_handle();
  int result = pthread_setaffinity_np(thread_handle, sizeof(cpu_set_t), &cpuset);
  
  if (result == 0) {
    std::cout << "Thread bound to CPU " << cpu_id << " successfully" << std::endl;
  } else {
    std::cerr << "Failed to bind thread to CPU " << cpu_id << ", error: " << result << std::endl;
  }
#else
  // 非Linux系统不执行绑定操作
  std::cout << "CPU binding is only supported on Linux systems" << std::endl;
#endif
}