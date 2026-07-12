#include "v4l2_output.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <libavutil/pixfmt.h>
#include <libavutil/imgutils.h>
#include <libavutil/frame.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

V4LOutput::V4LOutput() {}

V4LOutput::~V4LOutput() {
    close();
}

bool V4LOutput::open(const std::string& device_path) {
    if (fd_ >= 0) {
        close();  // Close existing device
    }
    
    device_path_ = device_path;
    
    // Open device in read-write mode for output
    fd_ = ::open(device_path.c_str(), O_RDWR | O_NONBLOCK);
    
    if (fd_ < 0) {
        std::cerr << "[V4L2] Failed to open device: " << device_path 
                  << " (" << strerror(errno) << ")" << std::endl;
        return false;
    }
    
    // Check capabilities
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    
    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "[V4L2] VIDIOC_QUERYCAP failed" << std::endl;
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    // Verify it's a video output device
    if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
        std::cerr << "[V4L2] Device is not a video output device" << std::endl;
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    std::cout << "[V4L2] Opened device: " << device_path 
              << " (driver: " << (char*)cap.driver 
              << ", card: " << (char*)cap.card << ")" << std::endl;
    
    return true;
}

void V4LOutput::close() {
    unsetup_buffers();
    
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        
        std::cout << "[V4L2] Closed device: " << device_path_ << std::endl;
    }
}

bool V4LOutput::configure(int width, int height, uint32_t pixel_format) {
    if (fd_ < 0) {
        std::cerr << "[V4L2] Device not open, cannot configure" << std::endl;
        return false;
    }
    
    // Unsetup existing buffers if format is changing
    if (width_ != width || height_ != height || pixelformat_ != pixel_format) {
        unsetup_buffers();
    }
    
    width_ = width;
    height_ = height;
    pixelformat_ = pixel_format;
    
    // Set video format
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixel_format;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    
    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[V4L2] VIDIOC_S_FMT failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Verify what was actually set
    if (fmt.fmt.pix.width != width || fmt.fmt.pix.height != height) {
        std::cout << "[V4L2] Format adjusted: requested " << width << "x" << height 
                  << ", got " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << std::endl;
        width_ = fmt.fmt.pix.width;
        height_ = fmt.fmt.pix.height;
    }
    
    // Setup memory-mapped buffers
    if (!setup_buffers()) {
        return false;
    }
    
    std::cout << "[V4L2] Configured: " << width_ << "x" << height_ 
              << ", format=" << std::hex << pixel_format << std::dec << std::endl;
    
    return true;
}

bool V4LOutput::setup_buffers() {
    // Request buffers
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = NUM_BUFFERS;
    
    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[V4L2] VIDIOC_REQBUFS failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Map buffers
    for (int i = 0; i < req.count && i < NUM_BUFFERS; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[V4L2] VIDIOC_QUERYBUF failed for buffer " << i 
                      << ": " << strerror(errno) << std::endl;
            return false;
        }
        
        buffers_[i].start = mmap(nullptr, buf.length, 
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd_, buf.m.offset);
        
        if (buffers_[i].start == MAP_FAILED) {
            std::cerr << "[V4L2] mmap failed for buffer " << i 
                      << ": " << strerror(errno) << std::endl;
            return false;
        }
        
        buffers_[i].length = buf.length;
    }
    
    current_buffer_ = 0;
    
    // Enable streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[V4L2] VIDIOC_STREAMON failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}

void V4LOutput::unsetup_buffers() {
    if (fd_ >= 0) {
        try {
            // Stop streaming
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            ioctl(fd_, VIDIOC_STREAMOFF, &type);
            
            // Unmap buffers
            for (int i = 0; i < NUM_BUFFERS; i++) {
                if (buffers_[i].start && buffers_[i].start != MAP_FAILED) {
                    munmap(buffers_[i].start, buffers_[i].length);
                    buffers_[i].start = nullptr;
                    buffers_[i].length = 0;
                }
            }
            
            // Request zero buffers to free driver resources
            struct v4l2_requestbuffers req;
            memset(&req, 0, sizeof(req));
            req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            req.memory = V4L2_MEMORY_MMAP;
            req.count = 0;
            ioctl(fd_, VIDIOC_REQBUFS, &req);
            
        } catch (...) {}
    }
}

bool V4LOutput::write_frame(AVFrame* frame) {
    if (!frame || fd_ < 0) return false;
    
    // Convert AVFrame to raw data based on format and copy to buffer
    // BGR24 = 3 bytes per pixel; av_image_get_buffer_size may be inline in older FFmpeg
    int frame_size = width_ * height_ * 3;
    
    if (frame_size > 0 && frame_size <= static_cast<int>(buffers_[current_buffer_].length)) {
        memcpy(buffers_[current_buffer_].start, frame->data[0], frame_size);
        return write_raw_frame(static_cast<const uint8_t*>(buffers_[current_buffer_].start), 
                               frame_size);
    } else {
        // Fallback: use frame linesize-based copy for non-contiguous data
        uint8_t* dst = static_cast<uint8_t*>(buffers_[current_buffer_].start);
        const uint8_t* src = frame->data[0];
        int linesize = frame->linesize[0];
        
        for (int y = 0; y < height_; y++) {
            memcpy(dst + y * width_ * 3, src + y * linesize, width_ * 3);
        }
        
        return write_raw_frame(dst, width_ * height_ * 3);
    }
}

bool V4LOutput::write_raw_frame(const uint8_t* data, size_t size) {
    if (!data || size == 0 || fd_ < 0) return false;
    
    // Standard V4L2 output flow: dequeue → fill → queue
    // Without DQBUF, buffers accumulate and QBUF eventually fails.
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;
    
    // Block until a buffer is available from the consumer side
    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno != EAGAIN && errno != EIO) {
            std::cerr << "[V4L2] VIDIOC_DQBUF failed: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    if ((size_t)buf.length < size) {
        std::cerr << "[V4L2] Buffer too small: " << buf.length 
                  << " < " << size << std::endl;
        return false;
    }
    
    // Copy data into the dequeued buffer
    memcpy(buffers_[buf.index].start, data, size);
    
    // Queue the filled buffer back
    buf.bytesused = size;
    buf.field = V4L2_FIELD_NONE;
    buf.timestamp.tv_sec = 0;
    buf.timestamp.tv_usec = 0;
    
    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        std::cerr << "[V4L2] VIDIOC_QBUF failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    frames_written_.fetch_add(1);
    return true;
}

bool V4LOutput::queue_buffer(int index) {
    if (index < 0 || index >= NUM_BUFFERS) return false;
    
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.bytesused = buffers_[index].length;
    buf.field = V4L2_FIELD_NONE;
    buf.timestamp.tv_sec = 0;
    buf.timestamp.tv_usec = 0;
    
    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        std::cerr << "[V4L2] VIDIOC_QBUF failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}
