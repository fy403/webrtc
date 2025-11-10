#include "webrtc_publisher.h"
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

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
  // 忽略 SIGPIPE 信号，防止网络连接断开时程序异常退出
  std::signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char *argv[]) {
  try {
    // 设置信号处理器
    setup_signal_handlers();

    Cmdline params(argc, argv);

    // 使用命令行参数或生成随机 ID
    std::string client_id = params.clientId(); // 使用新的client_id参数
    if (client_id.empty()) {
      client_id = randomId(4);
      std::cout << "Generated client ID: " << client_id << std::endl;
    } else {
      std::cout << "Using specified client ID: " << client_id << std::endl;
    }

    localId = client_id;

    WebRTCPublisher publisher(client_id, params);

    std::cout << "Starting WebRTC publisher..." << std::endl;
    publisher.start();
    std::cout << "WebRTC publisher started successfully. Press Ctrl+C to stop."
              << std::endl;

    // 主循环，检查关闭标志位
    while (!g_shutdown_requested.load()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(100)); // 更短的睡眠时间以便快速响应
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