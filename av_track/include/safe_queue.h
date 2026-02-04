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
  explicit SafeQueue(size_t capacity = 256, DeleterFunc deleter = nullptr);
  ~SafeQueue();

  // Blocking wait_push when full to avoid dropping or leaking items managed by
  // caller
  void wait_push(T item);

  bool try_push(T item);
  bool try_pop(T &item);
  void wait_pop(T &item);

  bool empty() const;
  size_t size() const;
  size_t capacity() const;

  void set_deleter(DeleterFunc deleter);
  void clear();

  // 停止队列：清空现有元素并唤醒所有等待线程，使其尽快退出
  void stop();

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
