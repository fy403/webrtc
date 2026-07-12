#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <string>
#include <vector>
#include <optional>

// Configuration structure for WebRTC receiver
struct Config {
    // Signaling server settings
    std::string signaling_url = "ws://tx.fy403.cn:8000";
    
    // ICE/STUN/TURN servers
    struct {
        std::string stun_server = "stun.l.google.com:19302";
        std::string turn_server = "turn:tx.fy403.cn:3478?transport=udp";
        std::string turn_username = "fy403";
        std::string turn_password = "qwertyuiop";
    } ice;
    
    // Connection settings
    std::string remote_id;
    bool auto_reconnect = true;
    int reconnect_interval_min = 500;      // ms
    int reconnect_interval_max = 3000;     // ms
    int max_reconnect_attempts = 120;
    
    // Video output settings
    std::string video_device = "/dev/video10";  // v4l2loopback device
    std::string video_format = "RGB24";
    
    // Audio output settings
    struct {
        std::string sink_name = "webrtc_receiver_sink";
        int sample_rate = 48000;
        int channels = 2;
    } audio;
    
    // Latency tracking
    bool enable_latency_tracking = true;
    double stats_interval = 1.0;  // seconds
};

class ConfigParser {
public:
    // Load config from file (returns default if file doesn't exist)
    static Config load(const std::string& filepath);
    
    // Parse command line arguments, override config values
    static void parse_args(int argc, char* argv[], Config& config);
    
    // Validate configuration, return error message or empty string on success
    static std::string validate(const Config& config);

private:
    static std::vector<std::string> tokenize(const std::string& line, char delimiter = '=');
};

#endif // CONFIG_PARSER_H
