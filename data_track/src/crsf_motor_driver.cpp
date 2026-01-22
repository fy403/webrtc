#include "crsf_motor_driver.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <time.h>
#include <errno.h>

// CRSF协议相关定义
#define CRSF_FRAME_SIZE_MAX 64
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16

CRSFMotorDriver::CRSFMotorDriver(const std::string &port,
                                 uint16_t servo_min_pulse,
                                 uint16_t servo_max_pulse,
                                 uint16_t servo_neutral_pulse,
                                 float servo_min_angle,
                                 float servo_max_angle,
                                 uint8_t servo_channel,
                                 uint16_t esc_min_pulse,
                                 uint16_t esc_max_pulse,
                                 uint16_t esc_neutral_pulse,
                                 bool esc_reversible,
                                 uint8_t esc_channel)
    : port_(port),
      serial_(std::make_unique<SerialPort>(port)),
      servo_config_(std::make_unique<ServoConfig>(
          servo_min_pulse, servo_max_pulse, servo_neutral_pulse,
          servo_min_angle, servo_max_angle, servo_channel,
          "CRSF Servo")),
      esc_config_(std::make_unique<ESCConfig>(
          esc_min_pulse, esc_max_pulse, esc_neutral_pulse,
          esc_reversible, esc_channel, "CRSF ESC")),
      crsf_config_(std::make_unique<CRSFConfig>()),
      running_(false) {
    channels_.resize(crsf_config_->channel_count, crsf_config_->channel_neutral);
}

CRSFMotorDriver::~CRSFMotorDriver() {
    disconnect();
}

bool CRSFMotorDriver::connect() {
    if (!serial_->openPort()) {
        return false;
    }

    // 启动后台发送线程
    running_ = true;
    send_thread_ = std::thread(&CRSFMotorDriver::sendThreadFunc, this);

    std::cout << "CRSF Motor Driver connected successfully" << std::endl;
    return true;
}

void CRSFMotorDriver::disconnect() {
    if (running_) {
        running_ = false;
        if (send_thread_.joinable()) {
            send_thread_.join();
        }
    }
}

uint8_t CRSFMotorDriver::calculateCRC8(const uint8_t *data, size_t length) {
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

void CRSFMotorDriver::packChannels11bit(uint8_t *buffer, const std::vector<uint16_t> &ch_values, int num_channels) {
    memset(buffer, 0, 22);

    int bit_index = 0;
    for (int i = 0; i < num_channels && i < 16; i++) {
        uint16_t value = (i < static_cast<int>(ch_values.size())) ? ch_values[i] : crsf_config_->channel_neutral;
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

void CRSFMotorDriver::createChannelsFrame(uint8_t *buffer, size_t &length) {
    buffer[0] = crsf_config_->sync_byte;
    buffer[1] = 24; // 数据长度: 22字节payload + 1字节type + 1字节CRC = 24
    buffer[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;

    packChannels11bit(&buffer[3], channels_, 16);

    uint8_t crc = calculateCRC8(&buffer[2], 23); // type + 22字节payload
    buffer[25] = crc;

    length = 26; // 整个帧长度: sync(1) + len(1) + type(1) + payload(22) + crc(1) = 26
}

void CRSFMotorDriver::sendThreadFunc() {
    uint8_t frame[CRSF_FRAME_SIZE_MAX];
    size_t length;

    const long period_nsec = 20000000; // 20ms = 20000000 纳秒

    struct timespec next_time;
    clock_gettime(CLOCK_MONOTONIC, &next_time);

    while (running_) {
        {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            createChannelsFrame(frame, length);
        }

        serial_->writeData(frame, length);

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

uint16_t CRSFMotorDriver::servoPwmToChannel(uint16_t pwm_us) {
    float ratio = static_cast<float>(pwm_us - servo_config_->min_pulse_width) /
                  (servo_config_->max_pulse_width - servo_config_->min_pulse_width);
    return static_cast<uint16_t>(crsf_config_->channel_min +
                                 ratio * (crsf_config_->channel_max - crsf_config_->channel_min));
}

uint16_t CRSFMotorDriver::escPwmToChannel(uint16_t pwm_us) {
    float ratio = static_cast<float>(pwm_us - esc_config_->min_pulse_width) /
                  (esc_config_->max_pulse_width - esc_config_->min_pulse_width);
    return static_cast<uint16_t>(crsf_config_->channel_min +
                                 ratio * (crsf_config_->channel_max - crsf_config_->channel_min));
}

uint16_t CRSFMotorDriver::angleToPWM(float angle) {
    float angle_range = servo_config_->max_angle - servo_config_->min_angle;
    if (!isSteeringServo()) {
        // 角度范围为零（转向马达模式），直接返回中位PWM
        return servo_config_->neutral_pulse_width;
    }
    float ratio = (angle - servo_config_->min_angle) / angle_range;
    return static_cast<uint16_t>(servo_config_->min_pulse_width +
                                 ratio * (servo_config_->max_pulse_width - servo_config_->min_pulse_width));
}

void CRSFMotorDriver::setThrottlePWM(uint16_t pwm_us) {
    uint16_t min_pw = esc_config_->min_pulse_width;
    uint16_t max_pw = esc_config_->max_pulse_width;

    if (pwm_us < min_pw)
        pwm_us = min_pw;
    if (pwm_us > max_pw)
        pwm_us = max_pw;

    uint8_t ch_idx = esc_config_->channel - 1;
    if (ch_idx < channels_.size()) {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        channels_[ch_idx] = escPwmToChannel(pwm_us);
    }
}

void CRSFMotorDriver::setServoAngle(float angle) {
    if (angle < servo_config_->min_angle)
        angle = servo_config_->min_angle;
    if (angle > servo_config_->max_angle)
        angle = servo_config_->max_angle;

    uint16_t pwm_us = angleToPWM(angle);
    uint8_t ch_idx = servo_config_->channel - 1;
    if (ch_idx < channels_.size()) {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        channels_[ch_idx] = servoPwmToChannel(pwm_us);
    }
}

void CRSFMotorDriver::setMotorPercent(int motor_id, int percent) {
    // CRSF驱动中，motor_id 1 表示电调（前后），motor_id 2 表示舵机（左右）
    // 但为了兼容接口，我们按照语义处理
    if (motor_id == 1 || motor_id == esc_config_->channel) {
        setFrontBackPercent(percent);
    } else if (motor_id == 2 || motor_id == servo_config_->channel) {
        setLeftRightPercent(percent);
    }
}

void CRSFMotorDriver::setFrontBackPercent(int percent) {
    // 电调控制前后：将百分比转换为PWM脉宽
    uint16_t min_pw = esc_config_->min_pulse_width;
    uint16_t max_pw = esc_config_->max_pulse_width;
    uint16_t neutral_pw = esc_config_->neutral_pulse_width;

    int clamped = std::max(-100, std::min(100, percent));
    uint16_t pwm_us;

    if (clamped >= 0) {
        // 正向油门: 中位到最大
        pwm_us = neutral_pw + static_cast<uint16_t>((clamped / 100.0f) * (max_pw - neutral_pw));
    } else {
        // 反向或刹车
        if (esc_config_->reversible) {
            float offset = (clamped / 100.0f) * (neutral_pw - min_pw);
            pwm_us = static_cast<uint16_t>(neutral_pw + offset);
        } else {
            pwm_us = neutral_pw;
        }
    }
    setThrottlePWM(pwm_us);
}

void CRSFMotorDriver::setLeftRightPercent(int percent) {
    // 判断转向类型：舵机还是马达
    if (isSteeringServo()) {
        // 舵机控制左右：将百分比转换为角度
        // 假设 0% = 中间角度，-100% = 最小角度，+100% = 最大角度
        int clamped = std::max(-100, std::min(100, percent));

        float min_angle = servo_config_->min_angle;
        float max_angle = servo_config_->max_angle;
        float center_angle = (min_angle + max_angle) / 2.0f;
        float angle_range = max_angle - min_angle;

        float angle = center_angle + (clamped / 100.0f) * (angle_range / 2.0f);
        setServoAngle(angle);
    } else {
        // 马达控制转向：使用类似电调的方式
        setSteeringMotor(percent);
    }
}

bool CRSFMotorDriver::isSteeringServo() {
    // 当 min_angle == max_angle 时，表示转向马达而非舵机
    static bool is = std::abs(servo_config_->min_angle - servo_config_->max_angle) > 0.001f;
    return is;
}

void CRSFMotorDriver::setSteeringMotor(int percent) {
    // 马达控制转向：将百分比转换为PWM脉宽，类似电调逻辑
    uint16_t min_pw = servo_config_->min_pulse_width;
    uint16_t max_pw = servo_config_->max_pulse_width;
    uint16_t neutral_pw = servo_config_->neutral_pulse_width;

    int clamped = std::max(-100, std::min(100, percent));
    uint16_t pwm_us;

    if (clamped >= 0) {
        // 正向：中位到最大
        pwm_us = neutral_pw + static_cast<uint16_t>((clamped / 100.0f) * (max_pw - neutral_pw));
    } else {
        // 反向：中位到最小
        float offset = (clamped / 100.0f) * (neutral_pw - min_pw);
        pwm_us = static_cast<uint16_t>(neutral_pw + offset);
    }

    // 将PWM值设置到转向通道
    uint16_t clamped_pwm = std::max(min_pw, std::min(max_pw, pwm_us));
    uint8_t ch_idx = servo_config_->channel - 1;
    if (ch_idx < channels_.size()) {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        channels_[ch_idx] = servoPwmToChannel(clamped_pwm);
    }
}
