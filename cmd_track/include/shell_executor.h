#ifndef SHELL_EXECUTOR_H
#define SHELL_EXECUTOR_H

#include "rtc/rtc.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// Persistent interactive shell session backed by a PTY (forkpty).
/// DataChannel  open → forkpty bash → bidirectional binary streaming.
class ShellSession {
public:
    /// @param rows  Initial terminal rows
    /// @param cols  Initial terminal columns
    /// @param dc    DataChannel used for bidirectional transport
    ShellSession(int rows, int cols, std::shared_ptr<rtc::DataChannel> dc);
    ~ShellSession();

    ShellSession(const ShellSession&) = delete;
    ShellSession& operator=(const ShellSession&) = delete;

    /// Feed raw keystroke bytes from the browser into the PTY master.
    void write(const std::vector<std::byte>& data);

    /// Resize the PTY window.
    void resize(int rows, int cols);

    /// Send a signal to the shell process (e.g. SIGINT, SIGTERM).
    void kill(int sig);

    /// Whether the session is still alive.
    bool isRunning() const { return running_.load(); }

private:
    void readLoop();       // select() → read master → dc->send(binary)
    void cleanup();

    int           master_fd_  = -1;
    pid_t         child_pid_  = -1;
    std::shared_ptr<rtc::DataChannel> dc_;
    std::thread   read_thread_;
    std::atomic<bool> running_{false};
    std::mutex    write_mutex_;
};

#endif // SHELL_EXECUTOR_H
