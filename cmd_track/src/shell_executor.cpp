#include "shell_executor.h"

#include <nlohmann/json.hpp>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using nlohmann::json;

// =============================================================================
// ShellSession
// =============================================================================

ShellSession::ShellSession(int rows, int cols,
                           std::shared_ptr<rtc::DataChannel> dc)
    : dc_(std::move(dc)) {

    struct winsize ws;
    std::memset(&ws, 0, sizeof(ws));
    ws.ws_row = static_cast<unsigned short>(rows > 0 ? rows : 24);
    ws.ws_col = static_cast<unsigned short>(cols > 0 ? cols : 80);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pid_t pid = forkpty(&master_fd_, nullptr, nullptr, &ws);
    if (pid < 0) {
        std::cerr << "[ShellSession] forkpty failed: " << strerror(errno)
                  << std::endl;
        return;
    }

    if (pid == 0) {
        // ── child process ──────────────────────────────────────────
        // Set up environment
        const char* term = getenv("TERM");
        if (!term || term[0] == '\0') {
            setenv("TERM", "xterm-256color", 1);
        }

        // Try $SHELL, fall back to /bin/bash, then /bin/sh
        const char* shell = getenv("SHELL");
        if (!shell || shell[0] == '\0') shell = "/bin/bash";

        execlp(shell, shell, "-l", (char*)nullptr);

        // If execlp returns, something went wrong
        perror("[ShellSession] execlp failed, falling back to /bin/sh");
        execlp("/bin/sh", "/bin/sh", (char*)nullptr);
        _exit(127);
    }

    // ── parent process ──────────────────────────────────────────────
    child_pid_ = pid;
    running_.store(true);

    // Send session-started notification
    json started = {
        {"type", "shell_session"},
        {"status", "started"}
    };
    try {
        if (dc_ && dc_->isOpen()) dc_->send(started.dump());
    } catch (...) {}

    std::cout << "[ShellSession] PTY started, pid=" << child_pid_
              << ", fd=" << master_fd_ << ", " << cols << "x" << rows
              << std::endl;

    read_thread_ = std::thread(&ShellSession::readLoop, this);
}

ShellSession::~ShellSession() {
    cleanup();
}

void ShellSession::cleanup() {
    running_.store(false);

    // Close master fd to trigger child exit
    if (master_fd_ >= 0) {
        // Write EOF-like signal (SIGHUP is more graceful)
        close(master_fd_);
        master_fd_ = -1;
    }

    if (read_thread_.joinable()) {
        read_thread_.join();
    }

    // Reap child
    if (child_pid_ > 0) {
        int status = 0;
        // Send SIGKILL if still alive after 2 seconds
        int waited = 0;
        while (waited < 20) {  // max 2 seconds
            pid_t result = waitpid(child_pid_, &status, WNOHANG);
            if (result == child_pid_) break;
            if (result < 0 && errno != EINTR) break;
            usleep(100000);  // 100ms
            waited++;
        }
        if (waited >= 20) {
            ::kill(child_pid_, SIGKILL);
            waitpid(child_pid_, &status, 0);
        }
        child_pid_ = -1;

        // Notify browser of session end
        int exitCode = -1;
        if (WIFEXITED(status)) exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exitCode = 128 + WTERMSIG(status);

        json done = {
            {"type", "shell_session"},
            {"status", "ended"},
            {"exit_code", exitCode}
        };
        try {
            if (dc_ && dc_->isOpen()) dc_->send(done.dump());
        } catch (...) {}

        std::cout << "[ShellSession] Session ended, exit_code="
                  << exitCode << std::endl;
    }
}

void ShellSession::readLoop() {
    char buf[8192];

    while (running_.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(master_fd_, &rfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms timeout

        int ret = select(master_fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[ShellSession] select error: " << strerror(errno)
                      << std::endl;
            break;
        }
        if (ret == 0) continue;  // timeout

        if (FD_ISSET(master_fd_, &rfds)) {
            ssize_t n = read(master_fd_, buf, sizeof(buf));
            if (n <= 0) {
                // EOF or error → child exited
                break;
            }

            // Send raw PTY output as binary through DataChannel
            if (dc_ && dc_->isOpen()) {
                try {
                    std::vector<std::byte> binData(
                        reinterpret_cast<std::byte*>(buf),
                        reinterpret_cast<std::byte*>(buf + n));
                    dc_->send(std::move(binData));
                } catch (const std::exception& e) {
                    std::cerr << "[ShellSession] dc send error: "
                              << e.what() << std::endl;
                    break;
                }
            }
        }
    }

    // Thread exiting → session is over
    if (running_.load()) {
        running_.store(false);
        cleanup();
    }
}

void ShellSession::write(const std::vector<std::byte>& data) {
    if (!running_.load() || master_fd_ < 0) return;

    std::lock_guard<std::mutex> lock(write_mutex_);
    const char* ptr = reinterpret_cast<const char*>(data.data());
    size_t remaining = data.size();

    while (remaining > 0) {
        ssize_t n = ::write(master_fd_, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "[ShellSession] write error: " << strerror(errno)
                      << std::endl;
            break;
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
}

void ShellSession::resize(int rows, int cols) {
    if (!running_.load() || master_fd_ < 0) return;
    if (rows <= 0 || cols <= 0) return;

    struct winsize ws;
    std::memset(&ws, 0, sizeof(ws));
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);

    if (ioctl(master_fd_, TIOCSWINSZ, &ws) < 0) {
        std::cerr << "[ShellSession] TIOCSWINSZ failed: " << strerror(errno)
                  << std::endl;
    } else {
        std::cout << "[ShellSession] Resized to " << cols << "x" << rows
                  << std::endl;
    }
}

void ShellSession::kill(int sig) {
    if (child_pid_ > 0 && running_.load()) {
        ::kill(child_pid_, sig);
        std::cout << "[ShellSession] Sent signal " << sig
                  << " to pid " << child_pid_ << std::endl;
    }
}
