#include "safe_queue.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

template <typename T>
SafeQueue<T>::SafeQueue(size_t capacity, DeleterFunc deleter)
    : capacity_(capacity == 0 ? 1 : capacity),
      buffer_(capacity_ > 0 ? capacity_ : 1), head_(0), tail_(0), count_(0),
      deleter_(deleter), is_running_(true) {}

template <typename T> SafeQueue<T>::~SafeQueue() {
  clear();
}

template <typename T> void SafeQueue<T>::wait_push(T item) {
  std::unique_lock<std::mutex> lock(mutex_);
  condition_.wait(lock, [this] { return count_ < capacity_ || !is_running_; });
  if (!is_running_) {
    return;
  }
  buffer_[tail_] = std::move(item);
  tail_ = (tail_ + 1) % capacity_;
  ++count_;
  lock.unlock();
  condition_.notify_one();
}

template <typename T> bool SafeQueue<T>::try_push(T item) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (!is_running_) {
    return false;
  }
  if (count_ < capacity_) {
    buffer_[tail_] = std::move(item);
    tail_ = (tail_ + 1) % capacity_;
    ++count_;
    lock.unlock();
    condition_.notify_one();
    return true;
  }
  return false;
}

template <typename T> bool SafeQueue<T>::try_pop(T &item) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (count_ == 0) {
    return false;
  }
  item = std::move(buffer_[head_]);
  head_ = (head_ + 1) % capacity_;
  --count_;
  lock.unlock();
  condition_.notify_one();
  return true;
}

template <typename T> void SafeQueue<T>::wait_pop(T &item) {
  std::unique_lock<std::mutex> lock(mutex_);
  condition_.wait(lock, [this] { return count_ > 0 || !is_running_; });
  if (count_ == 0) {
    // 队列已停止且为空，返回一个默认值（对于指针类型即为 nullptr）
    item = T{};
    return;
  }
  item = std::move(buffer_[head_]);
  head_ = (head_ + 1) % capacity_;
  --count_;
  lock.unlock();
  condition_.notify_one();
}

template <typename T> bool SafeQueue<T>::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return count_ == 0;
}

template <typename T> size_t SafeQueue<T>::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return count_;
}

template <typename T> size_t SafeQueue<T>::capacity() const {
  return capacity_;
}

template <typename T> void SafeQueue<T>::set_deleter(DeleterFunc deleter) {
  std::lock_guard<std::mutex> lock(mutex_);
  deleter_ = deleter;
}

template <typename T> void SafeQueue<T>::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (deleter_) {
    for (size_t i = 0; i < count_; ++i) {
      size_t index = (head_ + i) % capacity_;
      deleter_(buffer_[index]);
    }
  }
  head_ = 0;
  tail_ = 0;
  count_ = 0;
  // 保持运行状态，仅清空队列
  condition_.notify_all();
}

template <typename T> void SafeQueue<T>::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (deleter_) {
    for (size_t i = 0; i < count_; ++i) {
      size_t index = (head_ + i) % capacity_;
      deleter_(buffer_[index]);
    }
  }
  head_ = 0;
  tail_ = 0;
  count_ = 0;
  is_running_ = false;
  condition_.notify_all();
}

// 显式实例化常用的类型，避免链接错误
template class SafeQueue<struct AVPacket*>;
template class SafeQueue<struct AVFrame*>;
