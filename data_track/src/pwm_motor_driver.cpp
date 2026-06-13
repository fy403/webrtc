#include "pwm_motor_driver.h"
#include "rc_protocol_v2.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <sys/stat.h>

PwmMotorDriver::PwmMotorDriver(int front_back_chip,
                               int left_right_chip,
                               uint64_t period_ns,
                               int front_back_channel,
                               int left_right_channel,
                               int proto_forward_idx,
                               int proto_turn_idx,
                               uint16_t front_back_neutral,
                               uint16_t left_right_neutral)
    : front_back_chip_(front_back_chip),
      left_right_chip_(left_right_chip),
      period_ns_(period_ns),
      front_back_channel_(front_back_channel),
      left_right_channel_(left_right_channel),
      proto_forward_idx_(proto_forward_idx),
      proto_turn_idx_(proto_turn_idx),
      front_back_neutral_pwm_(front_back_neutral),
      left_right_neutral_pwm_(left_right_neutral),
      front_back_duty_(front_back_neutral),
      left_right_duty_(left_right_neutral),
      front_back_initialized_(false),
      left_right_initialized_(false) {

    std::cout << "PwmMotorDriver created with:" << std::endl;
    std::cout << "  Front/Back: pwmchip" << front_back_chip_ << "/pwm" << front_back_channel_
              << " <- proto channel[" << proto_forward_idx_ << "]" << std::endl;
    std::cout << "  Left/Right:  pwmchip" << left_right_chip_ << "/pwm" << left_right_channel_
              << " <- proto channel[" << proto_turn_idx_ << "]" << std::endl;
    std::cout << "  Period: " << period_ns_ << "ns (" << (1000000000.0 / period_ns_) << "Hz)" << std::endl;
}

PwmMotorDriver::~PwmMotorDriver() {
    disconnect();
}

std::string PwmMotorDriver::getPwmBasePath(int chip) const {
    return "/sys/class/pwm/pwmchip" + std::to_string(chip) + "/";
}

bool PwmMotorDriver::connect() {
    std::cout << "正在初始化PWM电机驱动器..." << std::endl;

    // 初始化前后通道
    if (front_back_channel_ >= 0 && front_back_channel_ < 4) {
        if (!exportPwmChannel(front_back_chip_, front_back_channel_)) {
            std::cerr << "Failed to export front/back PWM channel" << std::endl;
            return false;
        }
        if (!setPeriod(front_back_chip_, front_back_channel_, period_ns_)) {
            std::cerr << "Failed to set period for front/back channel" << std::endl;
            return false;
        }
        setPolarity(front_back_chip_, front_back_channel_, "normal");
        setDutyCycleNs(front_back_chip_, front_back_channel_, pwmToDutyNs(front_back_neutral_pwm_));
        if (!enablePwm(front_back_chip_, front_back_channel_, true)) {
            std::cerr << "Failed to enable front/back PWM channel" << std::endl;
            return false;
        }
        front_back_initialized_ = true;
        std::cout << "前后控制PWM通道初始化成功" << std::endl;
    }

    // 初始化左右转向通道
    if (left_right_channel_ >= 0 && left_right_channel_ < 4) {
        if (!exportPwmChannel(left_right_chip_, left_right_channel_)) {
            std::cerr << "Failed to export left/right PWM channel" << std::endl;
            return false;
        }
        if (!setPeriod(left_right_chip_, left_right_channel_, period_ns_)) {
            std::cerr << "Failed to set period for left/right channel" << std::endl;
            return false;
        }
        setPolarity(left_right_chip_, left_right_channel_, "normal");
        setDutyCycleNs(left_right_chip_, left_right_channel_, pwmToDutyNs(left_right_neutral_pwm_));
        if (!enablePwm(left_right_chip_, left_right_channel_, true)) {
            std::cerr << "Failed to enable left/right PWM channel" << std::endl;
            return false;
        }
        left_right_initialized_ = true;
        std::cout << "左右转向PWM通道初始化成功" << std::endl;
    }

    std::cout << "PWM电机驱动器初始化完成" << std::endl;
    return true;
}

void PwmMotorDriver::disconnect() {
    std::cout << "正在关闭PWM电机驱动器..." << std::endl;

    if (front_back_initialized_) {
        setDutyCycleNs(front_back_chip_, front_back_channel_, pwmToDutyNs(front_back_neutral_pwm_));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        enablePwm(front_back_chip_, front_back_channel_, false);
        unexportPwmChannel(front_back_chip_, front_back_channel_);
        front_back_initialized_ = false;
    }

    if (left_right_initialized_) {
        setDutyCycleNs(left_right_chip_, left_right_channel_, pwmToDutyNs(left_right_neutral_pwm_));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        enablePwm(left_right_chip_, left_right_channel_, false);
        unexportPwmChannel(left_right_chip_, left_right_channel_);
        left_right_initialized_ = false;
    }

    std::cout << "PWM电机驱动器已关闭" << std::endl;
}

// ---------- raw PWM (1000~2000us) → duty_cycle_ns (1000000~2000000ns) ----------
uint64_t PwmMotorDriver::pwmToDutyNs(uint16_t pwm_us) {
    // 1000us = 1000000ns, 2000us = 2000000ns
    return static_cast<uint64_t>(pwm_us) * 1000ULL;
}

void PwmMotorDriver::setMotorPWM(int motor_id, uint16_t pwm_us) {
    int channel = motor_id - 1;
    if (channel == front_back_channel_) {
        front_back_duty_ = pwm_us;
        applyChannelDuty(true);
    } else if (channel == left_right_channel_) {
        left_right_duty_ = pwm_us;
        applyChannelDuty(false);
    }
}

void PwmMotorDriver::setFrontBackPWM(uint16_t pwm_us) {
    if (front_back_channel_ >= 0 && front_back_channel_ < 4) {
        front_back_duty_ = pwm_us;
        applyChannelDuty(true);

        std::cout << "前后: " << pwm_us << std::endl;
    }
}

void PwmMotorDriver::setLeftRightPWM(uint16_t pwm_us) {
    if (left_right_channel_ >= 0 && left_right_channel_ < 4) {
        left_right_duty_ = pwm_us;
        applyChannelDuty(false);

        std::cout << "左右: " << pwm_us << std::endl;
    }
}

// ---------- 协议入口 ----------
void PwmMotorDriver::applyControl(const RCProtocolV2::ControlFrame &frame) {
    constexpr double DEADZONE_US = 10.0;

    double forward = frame.channels[proto_forward_idx_];
    double turn    = frame.channels[proto_turn_idx_];

    if (std::fabs(forward - front_back_neutral_pwm_) < DEADZONE_US) forward = front_back_neutral_pwm_;
    if (std::fabs(turn    - left_right_neutral_pwm_) < DEADZONE_US) turn    = left_right_neutral_pwm_;

    setFrontBackPWM(static_cast<uint16_t>(forward));
    setLeftRightPWM(static_cast<uint16_t>(turn));
}

void PwmMotorDriver::stopAll() {
    setFrontBackPWM(front_back_neutral_pwm_);
    setLeftRightPWM(left_right_neutral_pwm_);
}

bool PwmMotorDriver::applyChannelDuty(bool is_front_back) {
    int chip, channel;
    uint16_t pwm_us;

    if (is_front_back) {
        if (!front_back_initialized_) return false;
        chip = front_back_chip_;
        channel = front_back_channel_;
        pwm_us = front_back_duty_;
    } else {
        if (!left_right_initialized_) return false;
        chip = left_right_chip_;
        channel = left_right_channel_;
        pwm_us = left_right_duty_;
    }

    uint64_t duty_ns = pwmToDutyNs(pwm_us);
    std::cout << "PWM通道 pwmchip" << chip << "/pwm" << channel << ": "
              << pwm_us << "us -> " << duty_ns << "ns" << std::endl;

    return setDutyCycleNs(chip, channel, duty_ns);
}

// ========== SysFS helpers ==========

bool PwmMotorDriver::exportPwmChannel(int chip, int channel) {
    std::string base_path = getPwmBasePath(chip);
    std::string export_path = base_path + "export";
    std::string channel_path = base_path + "pwm" + std::to_string(channel);

    struct stat st;
    if (stat(channel_path.c_str(), &st) == 0) {
        return true;
    }

    std::ofstream file(export_path);
    if (!file.is_open()) return false;
    file << channel;
    file.close();

    int retry = 0;
    while (retry < 10) {
        if (stat(channel_path.c_str(), &st) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retry++;
    }
    return false;
}

void PwmMotorDriver::unexportPwmChannel(int chip, int channel) {
    std::string base_path = getPwmBasePath(chip);
    std::string unexport_path = base_path + "unexport";
    std::ofstream file(unexport_path);
    if (file.is_open()) {
        file << channel;
        file.close();
    }
}

bool PwmMotorDriver::setPeriod(int chip, int channel, uint64_t period_ns) {
    std::string base_path = getPwmBasePath(chip);
    std::string path = base_path + "pwm" + std::to_string(channel) + "/period";
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << period_ns;
    return file.good();
}

bool PwmMotorDriver::setDutyCycleNs(int chip, int channel, uint64_t duty_ns) {
    std::string base_path = getPwmBasePath(chip);
    std::string path = base_path + "pwm" + std::to_string(channel) + "/duty_cycle";
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << duty_ns;
    return file.good();
}

bool PwmMotorDriver::setPolarity(int chip, int channel, const std::string& polarity) {
    std::string base_path = getPwmBasePath(chip);
    std::string path = base_path + "pwm" + std::to_string(channel) + "/polarity";
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << polarity;
    return file.good();
}

bool PwmMotorDriver::enablePwm(int chip, int channel, bool enable) {
    std::string base_path = getPwmBasePath(chip);
    std::string path = base_path + "pwm" + std::to_string(channel) + "/enable";
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << (enable ? "1" : "0");
    return file.good();
}

bool PwmMotorDriver::writeSysfs(const std::string& path, const std::string& value) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << value;
    return file.good();
}

std::string PwmMotorDriver::readSysfs(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string value;
    std::getline(file, value);
    return value;
}
