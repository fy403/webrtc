#include "config_parser.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

Config ConfigParser::load(const std::string& filepath) {
    Config config;
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Return default config if file doesn't exist
        return config;
    }
    
    std::string line;
    std::string current_section;
    
    while (std::getline(file, line)) {
        // Strip Windows CRLF line endings (\r)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        
        // Check for section header [section]
        if (line[start] == '[') {
            size_t end = line.find(']', start);
            if (end != std::string::npos) {
                current_section = line.substr(start + 1, end - start - 1);
                // Convert to lowercase for case-insensitive matching
                std::transform(current_section.begin(), current_section.end(),
                              current_section.begin(), ::tolower);
            }
            continue;
        }
        
        // Parse key = value
        auto tokens = tokenize(line, '=');
        if (tokens.size() >= 2) {
            std::string key = tokens[0];
            std::string value = tokens[1];
            
            // Trim key (including Windows \r)
            size_t k_start = key.find_first_not_of(" \t\r");
            size_t k_end = key.find_last_not_of(" \t\r");
            if (k_start != std::string::npos) {
                key = key.substr(k_start, k_end - k_start + 1);
            }
            
            // Trim value (whitespace + surrounding quotes + \r)
            size_t v_start = value.find_first_not_of(" \t\r");
            size_t v_end = value.find_last_not_of(" \t\r");
            if (v_start != std::string::npos) {
                value = value.substr(v_start, v_end - v_start + 1);
            }
            // Strip surrounding double or single quotes
            if (value.size() >= 2) {
                if ((value.front() == '"' && value.back() == '"') ||
                    (value.front() == '\'' && value.back() == '\'')) {
                    value = value.substr(1, value.size() - 2);
                }
            }
            
            // Convert key to lowercase
            std::string lower_key = key;
            std::transform(lower_key.begin(), lower_key.end(),
                          lower_key.begin(), ::tolower);
            
            // Assign based on section and key
            if (current_section == "signaling" || current_section.empty()) {
                if (lower_key == "url") config.signaling_url = value;
            } else if (current_section == "ice") {
                if (lower_key == "stun_server") config.ice.stun_server = value;
                else if (lower_key == "turn_server") config.ice.turn_server = value;
                else if (lower_key == "turn_username") config.ice.turn_username = value;
                else if (lower_key == "turn_password") config.ice.turn_password = value;
            } else if (current_section == "connection") {
                if (lower_key == "remote_id") config.remote_id = value;
                else if (lower_key == "auto_reconnect") 
                    config.auto_reconnect = (value == "true" || value == "1");
                else if (lower_key == "reconnect_interval_min")
                    config.reconnect_interval_min = std::stoi(value);
                else if (lower_key == "reconnect_interval_max")
                    config.reconnect_interval_max = std::stoi(value);
                else if (lower_key == "max_reconnect_attempts")
                    config.max_reconnect_attempts = std::stoi(value);
            } else if (current_section == "video") {
                if (lower_key == "video_device") config.video_device = value;
                else if (lower_key == "video_format") config.video_format = value;
            } else if (current_section == "audio") {
                if (lower_key == "sink_name") config.audio.sink_name = value;
                else if (lower_key == "sample_rate") config.audio.sample_rate = std::stoi(value);
                else if (lower_key == "channels") config.audio.channels = std::stoi(value);
            } else if (current_section == "latency") {
                if (lower_key == "enable_latency_tracking")
                    config.enable_latency_tracking = (value == "true" || value == "1");
                else if (lower_key == "stats_interval")
                    config.stats_interval = std::stod(value);
            }
        }
    }
    
    return config;
}

void ConfigParser::parse_args(int argc, char* argv[], Config& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if ((arg == "--remote-id" || arg == "-r") && i + 1 < argc) {
            config.remote_id = argv[++i];
        } else if ((arg == "--signaling-url" || arg == "-s") && i + 1 < argc) {
            config.signaling_url = argv[++i];
        } else if ((arg == "--video-device" || arg == "-v") && i + 1 < argc) {
            config.video_device = argv[++i];
        } else if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            // Config file already loaded, skip
            ++i;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "WebRTC Virtual Camera Receiver\n"
                      << "Usage: webrtc_receiver [OPTIONS]\n"
                      << "  -r, --remote-id ID       Remote peer ID\n"
                      << "  -s, --signaling-url URL  Signaling server URL\n"
                      << "  -v, --video-device DEV   V4L2 device path\n"
                      << "  -c, --config FILE        Config file path\n"
                      << "  -h, --help               Show help\n";
            exit(0);
        }
    }
}

std::string ConfigParser::validate(const Config& config) {
    if (config.signaling_url.empty()) {
        return "Signaling URL is required";
    }
    if (config.signaling_url.find("ws://") != 0 && 
        config.signaling_url.find("wss://") != 0) {
        return "Signaling URL must use ws:// or wss:// protocol";
    }
    if (config.audio.sample_rate <= 0 || config.audio.sample_rate > 192000) {
        return "Invalid audio sample rate: " + std::to_string(config.audio.sample_rate);
    }
    if (config.audio.channels <= 0 || config.audio.channels > 8) {
        return "Invalid audio channel count: " + std::to_string(config.audio.channels);
    }
    return "";  // Valid
}

std::vector<std::string> ConfigParser::tokenize(const std::string& line, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        // Strip Windows CR from each token
        if (!token.empty() && token.back() == '\r') token.pop_back();
        tokens.push_back(token);
    }
    
    return tokens;
}
