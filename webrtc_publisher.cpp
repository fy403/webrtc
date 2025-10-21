// webrtc_publisher.cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <chrono>
#include <future>
#include <cstring>
#include <condition_variable>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "rtc/rtc.hpp"
#include "nlohmann/json.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/steady_timer.hpp>

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// 错误处理函数替代 av_err2str
std::string av_error_string(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return std::string(errbuf);
}

class VideoCapturer
{
public:
    VideoCapturer(const std::string &device = "/dev/video1")
        : device_(device), is_running_(false), track_ready_(false)
    {
        avdevice_register_all();
    }

    ~VideoCapturer()
    {
        stop();
    }

    bool start()
    {
        AVInputFormat *input_format = av_find_input_format("v4l2");
        if (!input_format)
        {
            std::cerr << "Cannot find V4L2 input format" << std::endl;
            return false;
        }

        AVDictionary *options = nullptr;
        av_dict_set(&options, "video_size", "640x480", 0);
        av_dict_set(&options, "framerate", "30", 0);
        av_dict_set(&options, "pixel_format", "yuyv422", 0);

        int ret = avformat_open_input(&format_context_, device_.c_str(), input_format, &options);
        if (ret < 0)
        {
            std::cerr << "Cannot open video device: " << av_error_string(ret) << std::endl;
            return false;
        }

        ret = avformat_find_stream_info(format_context_, nullptr);
        if (ret < 0)
        {
            std::cerr << "Cannot find stream info: " << av_error_string(ret) << std::endl;
            return false;
        }

        video_stream_index_ = -1;
        for (unsigned int i = 0; i < format_context_->nb_streams; i++)
        {
            if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                video_stream_index_ = i;
                break;
            }
        }

        if (video_stream_index_ == -1)
        {
            std::cerr << "Cannot find video stream" << std::endl;
            return false;
        }

        AVCodecParameters *codec_params = format_context_->streams[video_stream_index_]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
        if (!codec)
        {
            std::cerr << "Cannot find decoder" << std::endl;
            return false;
        }

        codec_context_ = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codec_context_, codec_params);

        ret = avcodec_open2(codec_context_, codec, nullptr);
        if (ret < 0)
        {
            std::cerr << "Cannot open codec: " << av_error_string(ret) << std::endl;
            return false;
        }

        // 初始化 H.264 编码器
        const AVCodec *h264_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!h264_codec)
        {
            std::cerr << "Cannot find H.264 encoder" << std::endl;
            return false;
        }

        encoder_context_ = avcodec_alloc_context3(h264_codec);
        encoder_context_->width = 640;
        encoder_context_->height = 480;
        encoder_context_->pix_fmt = AV_PIX_FMT_YUV420P;
        encoder_context_->time_base = {1, 30};
        encoder_context_->framerate = {30, 1};
        encoder_context_->gop_size = 10;
        encoder_context_->max_b_frames = 0;

        av_opt_set(encoder_context_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(encoder_context_->priv_data, "tune", "zerolatency", 0);

        ret = avcodec_open2(encoder_context_, h264_codec, nullptr);
        if (ret < 0)
        {
            std::cerr << "Cannot open H.264 encoder: " << av_error_string(ret) << std::endl;
            return false;
        }

        sws_context_ = sws_getContext(
            codec_context_->width, codec_context_->height, codec_context_->pix_fmt,
            encoder_context_->width, encoder_context_->height, encoder_context_->pix_fmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!sws_context_)
        {
            std::cerr << "Cannot create SwsContext" << std::endl;
            return false;
        }

        is_running_ = true;
        capture_thread_ = std::thread(&VideoCapturer::capture_loop, this);

        std::cout << "Video capture started successfully" << std::endl;
        return true;
    }

    void stop()
    {
        is_running_ = false;
        if (capture_thread_.joinable())
        {
            capture_thread_.join();
        }

        if (sws_context_)
        {
            sws_freeContext(sws_context_);
            sws_context_ = nullptr;
        }

        if (encoder_context_)
        {
            avcodec_free_context(&encoder_context_);
            encoder_context_ = nullptr;
        }

        if (codec_context_)
        {
            avcodec_free_context(&codec_context_);
            codec_context_ = nullptr;
        }

        if (format_context_)
        {
            avformat_close_input(&format_context_);
            format_context_ = nullptr;
        }
    }

    void set_track(std::shared_ptr<rtc::Track> track)
    {
        std::lock_guard<std::mutex> lock(track_mutex_);
        track_ = track;
        track_ready_ = true;
        track_cv_.notify_all();
    }

    void wait_for_track_ready()
    {
        std::unique_lock<std::mutex> lock(track_mutex_);
        track_cv_.wait(lock, [this]()
                       { return track_ready_.load(); });
    }

private:
    void capture_loop()
    {
        // 等待轨道准备好
        wait_for_track_ready();

        AVPacket *packet = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        AVFrame *yuv_frame = av_frame_alloc();

        yuv_frame->format = AV_PIX_FMT_YUV420P;
        yuv_frame->width = 640;
        yuv_frame->height = 480;
        yuv_frame->pts = 0;

        int ret = av_frame_get_buffer(yuv_frame, 0);
        if (ret < 0)
        {
            std::cerr << "Cannot allocate frame buffer: " << av_error_string(ret) << std::endl;
            av_frame_free(&yuv_frame);
            av_frame_free(&frame);
            av_packet_free(&packet);
            return;
        }

        std::cout << "Starting video capture loop" << std::endl;

        while (is_running_)
        {
            int ret = av_read_frame(format_context_, packet);
            if (ret < 0)
            {
                if (ret != AVERROR(EAGAIN))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                continue;
            }

            if (packet->stream_index == video_stream_index_)
            {
                ret = avcodec_send_packet(codec_context_, packet);
                if (ret < 0)
                {
                    av_packet_unref(packet);
                    continue;
                }

                while (ret >= 0)
                {
                    ret = avcodec_receive_frame(codec_context_, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    {
                        break;
                    }
                    else if (ret < 0)
                    {
                        break;
                    }

                    // 转换帧格式
                    sws_scale(sws_context_,
                              frame->data, frame->linesize, 0, frame->height,
                              yuv_frame->data, yuv_frame->linesize);

                    // 编码为 H.264
                    encode_and_send(yuv_frame);

                    yuv_frame->pts++;
                }
            }

            av_packet_unref(packet);
        }

        av_frame_free(&yuv_frame);
        av_frame_free(&frame);
        av_packet_free(&packet);
    }

    void encode_and_send(AVFrame *frame)
    {
        AVPacket *packet = av_packet_alloc();
        if (!packet)
        {
            return;
        }

        int ret = avcodec_send_frame(encoder_context_, frame);
        if (ret < 0)
        {
            av_packet_free(&packet);
            return;
        }

        while (ret >= 0)
        {
            ret = avcodec_receive_packet(encoder_context_, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                break;
            }

            std::lock_guard<std::mutex> lock(track_mutex_);
            if (track_ && track_->isOpen())
            {
                try
                {
                    // 发送 H.264 数据
                    auto data = reinterpret_cast<const std::byte *>(packet->data);
                    track_->send(data, packet->size);
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error sending video data: " << e.what() << std::endl;
                }
            }

            av_packet_unref(packet);
        }

        av_packet_free(&packet);
    }

private:
    std::string device_;
    std::atomic<bool> is_running_;
    std::atomic<bool> track_ready_;
    std::thread capture_thread_;
    std::mutex track_mutex_;
    std::condition_variable track_cv_;

    AVFormatContext *format_context_ = nullptr;
    AVCodecContext *codec_context_ = nullptr;
    AVCodecContext *encoder_context_ = nullptr;
    SwsContext *sws_context_ = nullptr;
    int video_stream_index_ = -1;

    std::shared_ptr<rtc::Track> track_;
};

class WebRTCPublisher
{
private:
    enum class SignalingState
    {
        Stable,
        HaveLocalOffer,
        HaveRemoteOffer,
        HaveLocalPranswer,
        HaveRemotePranswer
    };

    SignalingState signaling_state_ = SignalingState::Stable;
    std::mutex signaling_mutex_;
    std::string current_viewer_id_ = "";
    bool video_track_created_ = false;

public:
    WebRTCPublisher(const std::string &signaling_url, const std::string &client_id)
        : signaling_url_(signaling_url), client_id_(client_id), connected_(false),
          ioc_(std::make_shared<net::io_context>()), resolver_(*ioc_), ws_(*ioc_)
    {
        rtc::InitLogger(rtc::LogLevel::Info);

        // 配置WebRTC
        rtc::Configuration config;
        // STUN 服务器 - 使用字符串格式
        config.iceServers.push_back({"stun:stun.l.google.com:19302"});
        config.iceServers.push_back({"stun:stun1.l.google.com:19302"});
        config.iceServers.push_back({"stun:stun2.l.google.com:19302"});

        // TURN 服务器 - 使用带参数的构造函数
        config.iceServers.push_back(rtc::IceServer("192.168.137.1", 5349, "passuser", "rewqrecdscdscd", rtc::IceServer::RelayType::TurnUdp));
        // 强制只使用中继连接（禁用 STUN 和直连）
        // config.iceTransportPolicy = rtc::TransportPolicy::Relay;
        // 改进配置
        config.iceTransportPolicy = rtc::TransportPolicy::All;

        pc_ = std::make_shared<rtc::PeerConnection>(config);
        setup_peer_connection();
        parse_url(signaling_url_);
    }

    void start()
    {
        if (connect_with_timeout(std::chrono::seconds(30)))
        {
            // 启动IO上下文线程
            io_thread_ = std::thread([this]()
                                     { ioc_->run(); });

            // 启动消息读取线程
            message_thread_ = std::thread([this]()
                                          { read_messages(); });

            // 注册为发布者
            json register_msg = {
                {"type", "register_publisher"},
                {"client_id", client_id_}};
            send_websocket_message(register_msg.dump());

            std::cout << "Publisher registered successfully" << std::endl;
        }
    }

    void stop()
    {
        connected_ = false;
        video_capturer_.stop();
        if (pc_)
        {
            pc_->close();
        }
        try
        {
            ws_.close(websocket::close_code::normal);
        }
        catch (...)
        {
            // 忽略关闭错误
        }
        if (ioc_)
        {
            ioc_->stop();
        }
        if (io_thread_.joinable())
        {
            io_thread_.join();
        }
        if (message_thread_.joinable())
        {
            message_thread_.join();
        }
    }

private:
    void setup_peer_connection()
    {
        // 设置轨道状态回调
        pc_->onTrack([this](std::shared_ptr<rtc::Track> track)
                     { std::cout << "Track added: " << track->mid() << std::endl; });
        // offer answer
        pc_->onLocalDescription([this](rtc::Description description)
                                {
            std::lock_guard<std::mutex> lock(signaling_mutex_);
            std::cout << "Local description created: " << description.typeString() << std::endl;
            
            if (description.type() == rtc::Description::Type::Answer) {
                std::cout << "Sending answer to viewer: " << current_viewer_id_ << std::endl;

                std::string sdp = std::string(description);

                json message = {
                    {"type", "answer"},
                    {"client_id", client_id_},
                    {"answer", {
                        {"type", description.typeString()},
                        {"sdp", sdp}
                    }},
                    {"viewer_id", current_viewer_id_}
                };

                send_websocket_message(message.dump());
                signaling_state_ = SignalingState::Stable;
            } });

        pc_->onLocalCandidate([this](rtc::Candidate candidate)
                              {
            std::cout << "Local ICE candidate generated: " << candidate << std::endl;

            json message = {
                {"client_id", client_id_},
                {"type", "ice_candidate"},
                {"candidate", {{"candidate", std::string(candidate)}, {"sdpMid", candidate.mid()}}},
                {"target_id", current_viewer_id_}};

            send_websocket_message(message.dump()); });

        // ICE 状态回调
        pc_->onIceStateChange([this](rtc::PeerConnection::IceState state)
                              {
            switch (state) {
                case rtc::PeerConnection::IceState::Connected:
                    std::cout << "=== ICE CONNECTED SUCCESSFULLY ===" << std::endl;
                    break;
                case rtc::PeerConnection::IceState::Failed:
                    std::cerr << "=== ICE FAILED ===" << std::endl;
                    break;
                default:
                    std::cout << "ICE state: " << static_cast<int>(state) << std::endl;
                    break;
            } });

        pc_->onStateChange([this](rtc::PeerConnection::State state)
                           {
            switch (state) {
                case rtc::PeerConnection::State::Connected:
                    std::cout << "=== PEERCONNECTION CONNECTED SUCCESSFULLY ===" << std::endl;
                    break;
                case rtc::PeerConnection::State::Failed:
                    std::cerr << "=== PEERCONNECTION FAILED ===" << std::endl;
                    break;
                default:
                    std::cout << "PeerConnection state: " << static_cast<int>(state) << std::endl;
                    break;
            } });
    }

    void create_video_track()
    {
        if (video_track_created_)
        {
            std::cout << "Video track already created" << std::endl;
            return;
        }

        try
        {
            std::cout << "Creating video track..." << std::endl;

            // 创建视频描述
            rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
            video.addH264Codec(96);
            video.setBitrate(500000);
            video.addSSRC(1234567890, "video-send");

            auto video_track = pc_->addTrack(video);

            video_track->onOpen([this, video_track]()
                                {
                std::cout << "Video track is now open and ready to send data" << std::endl;
                video_capturer_.set_track(video_track);
                if (!video_capturer_.start()) {
                    std::cerr << "Failed to start video capture" << std::endl;
                    return;
                }
                std::cout << "Video capture started successfully" << std::endl; });

            video_track_created_ = true;
            std::cout << "Video track created successfully" << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error creating video track: " << e.what() << std::endl;
            throw;
        }
    }

    void parse_url(const std::string &url)
    {
        if (url.substr(0, 5) != "ws://" && url.substr(0, 6) != "wss://")
        {
            throw std::invalid_argument("Only ws:// or wss:// URLs are supported");
        }

        bool is_secure = (url.substr(0, 5) != "ws://");
        std::string rest = is_secure ? url.substr(6) : url.substr(5);

        size_t path_start = rest.find('/');
        if (path_start == std::string::npos)
        {
            host_ = rest;
            path_ = "/";
        }
        else
        {
            host_ = rest.substr(0, path_start);
            path_ = rest.substr(path_start);
            if (path_.empty())
            {
                path_ = "/";
            }
        }

        size_t colon_pos = host_.find(':');
        if (colon_pos != std::string::npos)
        {
            port_ = host_.substr(colon_pos + 1);
            host_ = host_.substr(0, colon_pos);
        }
        else
        {
            port_ = is_secure ? "443" : "80";
        }

        std::cout << "Parsed URL - Host: " << host_ << ", Port: " << port_ << ", Path: " << path_ << std::endl;
    }

    bool connect_with_timeout(std::chrono::seconds timeout_duration = std::chrono::seconds(20))
    {
        tcp::resolver::results_type endpoints;
        try
        {
            auto results = resolver_.resolve(host_, port_);
            std::cout << "DNS resolution successful for " << host_ << ":" << port_ << std::endl;
            endpoints = results;
        }
        catch (const std::exception &e)
        {
            std::cerr << "DNS resolution failed: " << e.what() << std::endl;
            return false;
        }

        bool connected = false;
        boost::system::error_code ec;

        std::thread connect_thread([&]()
                                   {
            try {
                ec = net::error::timed_out;
                net::connect(ws_.next_layer(), endpoints, ec);
                if (!ec) {
                    connected = true;
                }
            } catch (const std::exception& ex) {
                std::cerr << "Connect exception: " << ex.what() << std::endl;
                ec = boost::system::errc::make_error_code(boost::system::errc::io_error);
            } });

        auto start_time = std::chrono::steady_clock::now();
        while (!connected)
        {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= timeout_duration)
            {
                std::cerr << "Connection timeout after " << timeout_duration.count() << " seconds" << std::endl;
                ws_.next_layer().close();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        connect_thread.join();

        if (!connected)
        {
            return false;
        }

        if (ec)
        {
            std::cerr << "Connect failed: " << ec.message() << std::endl;
            return false;
        }

        try
        {
            ws_.set_option(websocket::stream_base::decorator(
                [](websocket::request_type &req)
                {
                    req.set(http::field::user_agent, "WebRTC Publisher v1.0");
                    req.set(http::field::sec_websocket_protocol, "protoo");
                }));

            std::string handshake_path = path_.empty() ? "/" : path_;
            ws_.handshake(host_ + ":" + port_, handshake_path);
            std::cout << "WebSocket handshake successful!" << std::endl;
            connected_ = true;
            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "WebSocket handshake failed: " << e.what() << std::endl;
            return false;
        }
    }

    void read_messages()
    {
        beast::flat_buffer buffer;
        while (connected_)
        {
            try
            {
                ws_.read(buffer);
                std::string message = beast::buffers_to_string(buffer.data());
                buffer.consume(buffer.size());

                std::cout << "Received message: " << message << std::endl;
                handle_signaling_message(message);
            }
            catch (const std::exception &e)
            {
                if (connected_)
                {
                    std::cerr << "Error reading WebSocket message: " << e.what() << std::endl;
                }
                break;
            }
        }
    }

    void handle_signaling_message(const std::string &message)
    {
        try
        {
            auto data = json::parse(message);
            std::string type = data["type"];

            std::lock_guard<std::mutex> lock(signaling_mutex_);

            if (type == "offer")
            {
                handle_offer(data);
            }
            else if (type == "ice_candidate")
            {
                handle_ice_candidate(data);
            }
            else if (type == "ready")
            {
                std::cout << "Signaling server is ready" << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error handling signaling message: " << e.what() << std::endl;
        }
    }

    void handle_offer(const json &data)
    {
        auto offer = data["offer"];
        std::string viewer_id = data["viewer_id"];
        current_viewer_id_ = viewer_id;

        std::cout << "Received offer from viewer: " << viewer_id << std::endl;

        try
        {
            std::string sdp = offer["sdp"].get<std::string>();
            std::string offer_type = offer["type"].get<std::string>();

            // 1. 设置远程描述
            std::cout << "Setting remote description..." << std::endl;
            pc_->setRemoteDescription(rtc::Description(sdp, offer_type));

            signaling_state_ = SignalingState::HaveRemoteOffer;

            // 2. 创建并添加视频轨道
            std::cout << "Creating video track..." << std::endl;
            create_video_track();

            // 3. 创建 answer
            std::cout << "Creating answer..." << std::endl;
            pc_->setLocalDescription();

            std::cout << "Answer created for viewer: " << viewer_id << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error handling offer from viewer: " << e.what() << std::endl;
        }
    }

    void handle_ice_candidate(const json &data)
    {
        auto candidate_data = data["candidate"];
        std::string candidate_str = candidate_data["candidate"].get<std::string>();
        std::string sdp_mid = candidate_data["sdpMid"].get<std::string>();

        std::cout << "Adding remote ICE candidate: " << candidate_str << std::endl;

        try
        {
            auto candidate = rtc::Candidate(candidate_str, sdp_mid);
            pc_->addRemoteCandidate(candidate);
            std::cout << "Remote candidate added successfully" << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error adding remote candidate: " << e.what() << std::endl;
        }
    }

    void send_websocket_message(const std::string &message)
    {
        if (connected_)
        {
            try
            {
                ws_.write(net::buffer(message));
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error sending WebSocket message: " << e.what() << std::endl;
            }
        }
    }

private:
    std::string signaling_url_;
    std::string client_id_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<net::io_context> ioc_;
    tcp::resolver resolver_;
    websocket::stream<tcp::socket> ws_;
    std::string host_;
    std::string port_;
    std::string path_;
    std::thread io_thread_;
    std::thread message_thread_;
    std::atomic<bool> connected_;

    VideoCapturer video_capturer_;
};

int main(int argc, char *argv[])
{
    try
    {
        if (argc < 2)
        {
            std::cerr << "Usage: " << argv[0] << " <ws://host:port/path>" << std::endl;
            return 1;
        }
        std::string signaling_url = argv[1];
        WebRTCPublisher publisher(signaling_url, "publisher_001");
        publisher.start();

        std::cout << "WebRTC Publisher started. Press Enter to exit..." << std::endl;
        std::cin.get();

        publisher.stop();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}