#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

// Thread-safe generic buffer (producer-consumer queue)
template<typename T>
class SafeQueue {
public:
    SafeQueue(size_t max_size = 30) 
        : max_size_(max_size), closed_(false) {}
    
    ~SafeQueue() { close(); }
    
    // Push item to queue (blocking if full, returns false if closed)
    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(mtx_);
        
        // Wait for space if queue is full (drop oldest if needed)
        while (!closed_ && queue_.size() >= max_size_) {
            // Drop front (oldest) item to make room
            queue_.pop();
            dropped_count_++;
        }
        
        if (closed_) return false;
        
        queue_.push(std::move(item));
        cv_.notify_one();
        return true;
    }
    
    // Pop item from queue (blocking until available or closed)
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mtx_);
        
        cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
        
        if (queue_.empty() && closed_) return false;
        
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    // Try pop without blocking
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    // Non-blocking push, returns false if full
    bool try_push(T&& item) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (closed_) return false;
        if (queue_.size() >= max_size_) {
            dropped_count_++;
            return false;
        }
        queue_.push(std::move(item));
        cv_.notify_one();
        return true;
    }
    
    // Get current size
    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }
    
    // Check if empty
    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }
    
    // Close the queue (wake up all waiters)
    void close() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            closed_ = true;
        }
        cv_.notify_all();
    }
    
    // Reset for reuse
    void reset() {
        std::lock_guard<std::mutex> lock(mtx_);
        while (!queue_.empty()) queue_.pop();
        closed_ = false;
        dropped_count_ = 0;
    }
    
    // Get number of dropped items
    uint64_t dropped_count() const { return dropped_count_.load(); }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        while (!queue_.empty()) queue_.pop();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    size_t max_size_;
    std::atomic<bool> closed_;
    std::atomic<uint64_t> dropped_count_{0};
};

#endif // FRAME_BUFFER_H
