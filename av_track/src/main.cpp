#include "webrtc_publisher.h"
#include "config_parser.h"
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <memory>
#include <stdexcept>

// 全局原子标志位，用于信号处理
std::atomic<bool> g_shutdown_requested{false};

// 信号处理函数
void signal_handler(int signal) {
  std::cout << "Received signal " << signal << ", shutting down gracefully..."
            << std::endl;
  g_shutdown_requested.store(true);
}

// 设置信号处理器
void setup_signal_handlers() {
  std::signal(SIGINT, signal_handler);  // Ctrl+C
  std::signal(SIGTERM, signal_handler); // 终止信号
  std::signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char *argv[]) {
  try {
    setup_signal_handlers();

    // 配置文件路径，可通过命令行第一个参数指定
    std::string config_file = "config.txt";
    if (argc > 1) {
      config_file = argv[1];
    }

    // 加载配置文件
    Config config(config_file);
    config.load();
    
    // 获取client_id
    std::string client_id = config.get("client_id", "");
    if (client_id.empty()) {
      client_id = randomId(4);
      std::cout << "Generated client ID: " << client_id << std::endl;
    } else {
      std::cout << "Using specified client ID: " << client_id << std::endl;
    }

    // 使用配置文件创建发布者
    WebRTCPublisher publisher(client_id, &config);

    std::cout << "Starting WebRTC publisher..." << std::endl;
    publisher.start();
    std::cout << "WebRTC publisher started successfully. Press Ctrl+C to stop."
              << std::endl;

    // 主循环
    while (!g_shutdown_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Stopping WebRTC publisher..." << std::endl;
    publisher.stop();
    std::cout << "WebRTC publisher stopped successfully." << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Application exited normally." << std::endl;
  return 0;
}
