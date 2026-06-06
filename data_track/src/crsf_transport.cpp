#include "crsf_transport.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <time.h>
#include <errno.h>

CRSFTransport::CRSFTransport(const std::string &port)
    : port_(port)
      , serial_(std::make_unique<SerialPort>(port))
      , running_(false) {
    // 初始化16通道为中位值（992 = CRSF neutral）
    channels_.resize(16, 992);
}

CRSFTransport::~CRSFTransport() {
    disconnect();
}

bool CRSFTransport::connect() {
    if (!serial_->openPort()) {
        return false;
    }

    running_ = true;
    send_thread_ = std::thread(&CRSFTransport::sendThreadFunc, this);

    std::cout << "CRSF Transport 启动成功" << std::endl;
    return true;
}

void CRSFTransport::disconnect() {
    if (running_) {
        running_ = false;
        if (send_thread_.joinable()) {
            send_thread_.join();
        }
    }
    // SerialPort 在析构时自动关闭，无需显式调用
}

void CRSFTransport::setChannel(uint8_t ch_index, uint16_t value) {
    if (ch_index < channels_.size()) {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        channels_[ch_index] = value;
    }
}

bool CRSFTransport::isRunning() const {
    return running_;
}

uint8_t CRSFTransport::calculateCRC8(const uint8_t *data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0xD5;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

void CRSFTransport::packChannels11bit(uint8_t *buffer, const std::vector<uint16_t> &ch_values, int num_channels) {
    memset(buffer, 0, 22);

    int bit_index = 0;
    for (int i = 0; i < num_channels && i < 16; i++) {
        uint16_t value = (i < static_cast<int>(ch_values.size())) ? ch_values[i] : 992;
        value &= 0x07FF; // 确保值在11位范围内 (0-2047)

        for (int bit = 0; bit < 11; bit++) {
            int byte_idx = bit_index >> 3;
            int bit_in_byte = bit_index & 0x07;

            if (value & (1 << bit)) {
                buffer[byte_idx] |= (1 << bit_in_byte);
            }
            bit_index++;
        }
    }
}

void CRSFTransport::createChannelsFrame(uint8_t *buffer, size_t &length) {
    buffer[0] = 0xC8; // CRSF sync byte
    buffer[1] = 24; // 数据长度: 22字节payload + 1字节type + 1字节CRC = 24
    buffer[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;

    packChannels11bit(&buffer[3], channels_, 16);

    uint8_t crc = calculateCRC8(&buffer[2], 23); // type + 22字节payload
    buffer[25] = crc;

    length = 26; // 整个帧长度: sync(1) + len(1) + type(1) + payload(22) + crc(1) = 26
}

void CRSFTransport::sendThreadFunc() {
    uint8_t frame[CRSF_FRAME_SIZE_MAX];
    size_t length;

    const long period_nsec = 20000000; // 20ms = 20000000 纳秒
    int frame_count = 0;

    struct timespec next_time;
    clock_gettime(CLOCK_MONOTONIC, &next_time);

    while (running_) {
        {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            createChannelsFrame(frame, length);
        }

        serial_->writeData(frame, length);

        // 每 50 帧（约 1 秒）打印一次实际发送的通道值
        // if (++frame_count % 50 == 0) {
        //     std::cout << "[CRSF TX] ";
        //     for (int i = 0; i < 8; i++) {
        //         std::cout << "CH" << (i + 1) << "=" << channels_[i];
        //         if (i < 7) std::cout << " ";
        //     }
        //     std::cout << std::endl;
        // }

        // 计算下一次发送时间（绝对时间）
        next_time.tv_nsec += period_nsec;
        if (next_time.tv_nsec >= 1000000000) {
            next_time.tv_sec += next_time.tv_nsec / 1000000000;
            next_time.tv_nsec = next_time.tv_nsec % 1000000000;
        }

        // 使用clock_nanosleep精确等待到下一个周期
        struct timespec remaining;
        int ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_time, &remaining);

        while (ret == EINTR && running_) {
            ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_time, &remaining);
        }

        if (ret != 0 && ret != EINTR) {
            std::cerr << "clock_nanosleep error: " << strerror(ret) << std::endl;
            break;
        }
    }
}
