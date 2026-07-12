/**
 * WebRTC Virtual Camera Receiver
 * ==============================
 * Receives H.264/H.265+Opus streams via WebRTC P2P and outputs to:
 *   - v4l2loopback virtual camera (/dev/videoN)
 *   - PulseAudio virtual microphone sink
 *
 * Usage:
 *   ./webrtc_receiver --remote-id <device_id> [--signaling-url <url>]
 *
 * Author: Generated for webrtc project
 */

#include <iostream>
#include <cstdlib>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <memory>
#include <random>

#include "config_parser.h"
#include "webrtc_receiver.h"
#include "signaling_client.h"
#include "video_pipeline.h"
#include "audio_pipeline.h"
#include "v4l2_output.h"
#include "pulse_audio_output.h"
#include "latency_tracker.h"

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};
std::atomic<int> g_signal_count{0};

// Signal handler for Ctrl+C (double press = force exit)
void signal_handler(int signal) {
    int count = ++g_signal_count;
    if (count >= 2) {
        std::cout << "\n[Main] Second signal received — force exit!" << std::endl;
        _Exit(0);  // Force exit immediately without cleanup (no atexit handlers)
    }
    std::cout << "\n[Main] Received signal " << signal 
              << ", shutting down (press Ctrl+C again to force)..." << std::endl;
    g_running.store(false);
}

// Print usage/help information
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -r, --remote-id ID       Remote peer ID (device) to connect to" << std::endl;
    std::cout << "  -s, --signaling-url URL  Signaling server WebSocket URL" << std::endl;
    std::cout << "  -v, --video-device DEV   V4L2 device path (default: /dev/video10)" << std::endl;
    std::cout << "  -c, --config FILE        Config file path (default: config.txt)" << std::endl;
    std::cout << "  -h, --help               Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " --remote-id ABC123" << std::endl;
    std::cout << "  " << program_name << " -r DEVICE_ID -s ws://localhost:8000" << std::endl;
    std::cout << std::endl;
    std::cout << "Config file format: see config.txt for details" << std::endl;
}

// Generate a random local ID for signaling
std::string generate_local_id(size_t length = 8) {
    static const char chars[] = 
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
    
    std::string id(length, '0');
    std::generate_n(id.begin(), length, [&]() { return chars[dist(gen)]; });
    
    return id;
}

int main(int argc, char* argv[]) {
    std::cout << "╔════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  WebRTC Virtual Camera Receiver v1.0      ║" << std::endl;
    std::cout << "║  Ubuntu/Linux Platform                    ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line arguments
    Config config;
    std::string config_path = "config.txt";
    
    // First check for --help
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // Load configuration from file
    config = ConfigParser::load(config_path);
    std::cout << "[Main] Loaded config from: " << config_path << std::endl;

    // Override with command line arguments
    ConfigParser::parse_args(argc, argv, config);

    // Validate configuration
    std::string validation_error = ConfigParser::validate(config);
    if (!validation_error.empty()) {
        std::cerr << "[Main] Configuration error: " << validation_error << std::endl;
        return 1;
    }

    // Print configuration summary
    std::cout << std::endl;
    std::cout << "┌─────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Configuration Summary                       │" << std::endl;
    std::cout << "├─────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ Signaling URL: " << config.signaling_url << std::endl;
    std::cout << "│ Remote ID:     " << (config.remote_id.empty() ? "<not set>" : config.remote_id) << std::endl;
    std::cout << "│ Video Device:  " << config.video_device << std::endl;
    std::cout << "│ PA Sink:       " << config.audio.sink_name << std::endl;
    std::cout << "│ Auto Reconnect:" << (config.auto_reconnect ? "ON" : "OFF") << std::endl;
    std::cout << "└─────────────────────────────────────────────┘" << std::endl;
    std::cout << std::endl;

    // Check if remote ID is specified
    if (config.remote_id.empty()) {
        std::cerr << "[Main] Error: Remote ID is required (--remote-id or remote_id in config)" << std::endl;
        std::cerr << "[Main] Use -h for help" << std::endl;
        return 1;
    }

    try {
        // Initialize components
        std::cout << "[Main] Initializing components..." << std::endl;

        // 1. Latency tracker
        LatencyTracker tracker;
        tracker.set_enabled(config.enable_latency_tracking);

        // 2. V4L2 output (virtual camera)
        V4LOutput v4l2_out;
        if (!v4l2_out.open(config.video_device)) {
            std::cerr << "[Main] Warning: Failed to open V4L2 device: " 
                      << config.video_device << std::endl;
            std::cerr << "[Main] Make sure v4l2loopback module is loaded:" << std::endl;
            std::cerr << "[Main]   sudo modprobe v4l2loopback video_nr=10 exclusive_caps=1" << std::endl;
            std::cerr << "[Main] Video output will be disabled." << std::endl;
        } else {
            std::cout <<("[Main] V4L2 device ready: " + config.video_device) << std::endl;
        }

        // 3. PulseAudio output (virtual microphone)
        PulseAudioOutput pa_out;
        if (!pa_out.open(config.audio.sink_name, 
                         config.audio.sample_rate, 
                         config.audio.channels))
        {
            std::cerr << "[Main] Warning: Failed to connect to PulseAudio" << std::endl;
            std::cerr << "[Main] Make sure PulseAudio is running:" << std::endl;
            std::cerr << "[Main]   pactl load-module module-virtual-sink" << std::endl;
            std::cerr << "[Main] Audio output will be disabled." << std::endl;
        } else {
            std::cout << ("[Main] PulseAudio connected: " + config.audio.sink_name) << std::endl;
        }

        // 4. Signaling client
        auto signaling = std::make_shared<SignalingClient>();
        
        // 5. Video pipeline
        VideoPipeline video_pipe(&tracker);
        if (v4l2_out.is_open()) {
            if (!video_pipe.init(&v4l2_out)) {
                std::cerr << "[Main] Failed to initialize video pipeline" << std::endl;
            } else {
                video_pipe.start();
                std::cout << "[Main] Video pipeline started" << std::endl;
            }
        }

        // 6. Audio pipeline
        AudioPipeline audio_pipe(&tracker);
        if (pa_out.is_open()) {
            if (!audio_pipe.init(&pa_out)) {
                std::cerr << "[Main] Failed to initialize audio pipeline" << std::endl;
            } else {
                audio_pipe.start();
                std::cout << "[Main] Audio pipeline started" << std::endl;
            }
        }

        // 7. WebRTC receiver (connects everything together)
        WebRTCReceiver receiver(&config);
        
        // Set up track callbacks to feed pipelines
        // CRITICAL: Use onFrame (not onMessage) because:
        //   - RTP depacketizer outputs complete NAL frames → delivered via onFrame
        //   - onMessage would still receive raw RTP packets (with 12-byte headers)
        std::atomic<size_t> vid_msg_count{0};
        std::atomic<size_t> aud_msg_count{0};
        receiver.set_on_video_track([&video_pipe, &vid_msg_count](rtc::Track& track, rtc::Description::Direction dir) {
            (void)dir;
            track.onFrame([&video_pipe, &vid_msg_count](rtc::binary data, rtc::FrameInfo info) {
                (void)info;
                size_t n = ++vid_msg_count;
                if (n <= 3 || n % 100 == 0)
                    std::cout << "[Main] Video frame #" << n << " (" << data.size() << " bytes)" << std::endl;
                video_pipe.push_rtp_data(data.data(), data.size());
            });
        });

        receiver.set_on_audio_track([&audio_pipe, &aud_msg_count](rtc::Track& track, rtc::Description::Direction dir) {
            (void)dir;
            track.onFrame([&audio_pipe, &aud_msg_count](rtc::binary data, rtc::FrameInfo info) {
                (void)info;
                size_t n = ++aud_msg_count;
                if (n <= 5 || n % 100 == 0)
                    std::cout << "[Main] Audio frame #" << n << " (" << data.size() << " bytes)" << std::endl;
                audio_pipe.push_rtp_data(data.data(), data.size());
            });
        });

        // Connect to signaling server
        std::string local_id = generate_local_id();
        std::cout << "[Main] Local ID: " << local_id << std::endl;

        if (!signaling->connect(config.signaling_url, local_id)) {
            std::cerr << "[Main] Failed to connect to signaling server" << std::endl;
            return 1;
        }

        // Wait for WebSocket connection to establish
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!receiver.init(signaling)) {
            std::cerr << "[Main] Failed to initialize WebRTC receiver" << std::endl;
            return 1;
        }

        // Start receiving from remote device
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        receiver.start_receive(config.remote_id);

        std::cout << std::endl;
        std::cout << "✅ Receiver is running! Waiting for connection from: " 
                  << config.remote_id << std::endl;
        std::cout << "   Press Ctrl+C to stop..." << std::endl;
        std::cout << std::endl;

        // Main loop - keep running until shutdown signal
        int stats_counter = 0;
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Print statistics periodically (every N iterations of 100ms)
            if (config.enable_latency_tracking && 
                ++stats_counter >= static_cast<int>(config.stats_interval * 10))
            {
                tracker.print_stats(config.stats_interval);
                stats_counter = 0;
                
                // Update counters in tracker for display
                tracker.video_frames_.store(video_pipe.get_frames_decoded());
                tracker.audio_packets_.store(audio_pipe.get_packets_received());
            }
        }

        // Graceful shutdown
        std::cout << "[Main] Shutting down..." << std::endl;
        
        receiver.shutdown();
        video_pipe.stop();
        audio_pipe.stop();
        
        v4l2_out.close();
        pa_out.close();

        std::cout << "[Main] Cleanup complete." << std::endl;
        
        // Print final statistics
        std::cout << std::endl;
        std::cout << "══════ FINAL STATISTICS ══════" << std::endl;
        std::cout << "Video frames decoded: " << video_pipe.get_frames_decoded() << std::endl;
        std::cout << "Video frames dropped: " << video_pipe.get_frames_dropped() << std::endl;
        std::cout << "Audio samples played: " << audio_pipe.get_samples_played() << std::endl;
        std::cout << "V4L2 frames written: " << v4l2_out.get_frames_written() << std::endl;
        std::cout << "PA bytes written:     " << pa_out.get_bytes_written() << std::endl;
        std::cout << "══════════════════════════════" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[Main] Fatal error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[Main] Goodbye!" << std::endl;
    return 0;
}
