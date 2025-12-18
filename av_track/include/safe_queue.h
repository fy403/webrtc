#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>

template <typename T> class SafeQueue {
public:
  using DeleterFunc = std::function<void(T&)>;

  // Circular buffer backed by std::vector with fixed capacity
  explicit SafeQueue(size_t capacity = 256, DeleterFunc deleter = nullptr)
      : capacity_(capacity == 0 ? 1 : capacity),
        buffer_(capacity_ > 0 ? capacity_ : 1), head_(0), tail_(0), count_(0),
        deleter_(deleter), is_running_(true) {}

  // Blocking wait_push when full to avoid dropping or leaking items managed by
  // caller
  void wait_push(T item) {
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

  bool try_push(T item) {
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

  bool try_pop(T &item) {
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

  void wait_pop(T &item) {
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

  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ == 0;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }

  size_t capacity() const { return capacity_; }

  void set_deleter(DeleterFunc deleter) {
    std::lock_guard<std::mutex> lock(mutex_);
    deleter_ = deleter;
  }

  void clear() {
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

  // 停止队列：清空现有元素并唤醒所有等待线程，使其尽快退出
  void stop() {
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

  ~SafeQueue() {
    clear();
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  size_t capacity_;
  std::vector<T> buffer_;
  size_t head_;
  size_t tail_;
  size_t count_;
  DeleterFunc deleter_;
  std::atomic<bool> is_running_;
};

#endif // SAFE_QUEUE_H