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

#include "rtc/rtc.hpp"

Capture::Capture(bool debug_enabled, size_t encode_queue_capacity,
                 size_t send_queue_capacity)
    : debug_enabled_(debug_enabled), is_running_(false), is_paused_(false),
      encode_queue_(encode_queue_capacity), send_queue_(send_queue_capacity) {
  avdevice_register_all();
}

Capture::~Capture() { stop(); }

bool Capture::is_running() { return is_running_; }
void Capture::set_track_callback(TrackCallback callback) {
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    track_callback_ = std::move(callback);
  }
  callback_cv_.notify_one();
}

void Capture::stop() {
  is_running_ = false;
  is_paused_ = false;

  // Notify all waiting threads
  encode_queue_.clear();
  send_queue_.clear();
  // Push sentinel nullptrs to unblock waiters
  encode_queue_.push(nullptr);
  send_queue_.push(nullptr);

  // Notify condition variables to wake up waiting threads
  callback_cv_.notify_all();

  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }

  if (encode_thread_.joinable()) {
    encode_thread_.join();
  }

  if (send_thread_.joinable()) {
    send_thread_.join();
  }

  // Clean up queues
  while (!encode_queue_.empty()) {
    AVFrame *frame;
    if (encode_queue_.pop(frame)) {
      av_frame_free(&frame);
    }
  }

  while (!send_queue_.empty()) {
    AVPacket *packet;
    if (send_queue_.pop(packet)) {
      av_packet_free(&packet);
    }
  }

  // Encoder context is managed by the Encoder class
  if (encoder_) {
    encoder_->close_encoder();
  }
}

void Capture::pause_capture() {
  is_paused_ = true;

  // 清空队列
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