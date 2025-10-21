#ifndef VIDEO_CAPTURER_H
#define VIDEO_CAPTURER_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <functional>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace rtc
{
    class Track;
}

class VideoCapturer
{
public:
    VideoCapturer(const std::string &device = "/dev/video1");
    ~VideoCapturer();
    bool start();
    void stop();
    using TrackCallback = std::function<void(const std::byte *data, size_t size)>;
    void set_track_callback(TrackCallback callback);

private:
    void capture_loop();
    void encode_and_send(AVFrame *frame);

    std::string device_;
    std::atomic<bool> is_running_;
    std::thread capture_thread_;
    TrackCallback track_callback_;

    AVFormatContext *format_context_ = nullptr;
    AVCodecContext *codec_context_ = nullptr;
    AVCodecContext *encoder_context_ = nullptr;
    SwsContext *sws_context_ = nullptr;
    int video_stream_index_ = -1;

    std::shared_ptr<rtc::Track> track_;
    
    // 添加互斥锁和条件变量用于同步回调函数的设置
    std::mutex callback_mutex_;
    std::condition_variable callback_cv_;
};

#endif // VIDEO_CAPTURER_H