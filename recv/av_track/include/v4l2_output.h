#ifndef V4L2_OUTPUT_H
#define V4L2_OUTPUT_H

#include <string>
#include <cstdint>
#include <atomic>

struct AVFrame;

class V4LOutput {
public:
    V4LOutput();
    ~V4LOutput();
    
    // Open V4L2 device for writing
    bool open(const std::string& device_path);
    
    // Close device
    void close();
    
    // Check if device is open
    bool is_open() const { return fd_ >= 0; }
    
    // Configure output format (resolution, pixel format)
    bool configure(int width, int height, uint32_t pixel_format);
    
    // Write a decoded frame (AVFrame in BGR24 or YUV format)
    bool write_frame(AVFrame* frame);
    
    // Write raw pixel data (must match configured format)
    bool write_raw_frame(const uint8_t* data, size_t size);
    
    // Get current configuration
    int get_width() const { return width_; }
    int get_height() const { return height_; }
    uint32_t get_pixel_format() const { return pixelformat_; }
    const std::string& get_device_path() const { return device_path_; }
    
    // Get statistics
    uint64_t get_frames_written() const { return frames_written_.load(); }

private:
    std::string device_path_;
    int fd_{-1};  // V4L2 file descriptor
    
    // Current format
    int width_{0};
    int height_{0};
    uint32_t pixelformat_{0};  // V4L2_PIX_FMT_* value
    
    // Buffer management (memory-mapped I/O)
    static constexpr int NUM_BUFFERS = 4;
    struct BufferInfo {
        void* start;
        size_t length;
    };
    BufferInfo buffers_[NUM_BUFFERS]{};
    int current_buffer_{0};
    
    // Statistics
    std::atomic<uint64_t> frames_written_{0};
    
    // Internal methods
    bool setup_buffers();
    void unsetup_buffers();
    bool queue_buffer(int index);
    bool dequeue_buffer();
};

#endif // V4L2_OUTPUT_H
