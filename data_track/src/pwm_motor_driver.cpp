#include "pwm_motor_driver.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

PwmMotorDriver::PwmMotorDriver(int front_back_chip,
                                 int left_right_chip,
                                 uint64_t period_ns,
                                 uint64_t duty_min_ns,
                                 uint64_t duty_max_ns,
                                 uint64_t duty_neutral_ns,
                                 int front_back_channel,
                                 int left_right_channel)
    : front_back_chip_(front_back_chip),
      left_right_chip_(left_right_chip),
      period_ns_(period_ns),
      duty_min_ns_(duty_min_ns),
      duty_max_ns_(duty_max_ns),
      duty_neutral_ns_(duty_neutral_ns),
      front_back_channel_(front_back_channel),
      left_right_channel_(left_right_channel),
      front_back_duty_(0),
      left_right_duty_(0),
      front_back_initialized_(false),
      left_right_initialized_(false) {

    std::cout << "PwmMotorDriver created with:" << std::endl;
    std::cout << "  Front/Back: pwmchip" << front_back_chip_ << "/pwm" << front_back_channel_ << std::endl;
    std::cout << "  Left/Right:  pwmchip" << left_right_chip_ << "/pwm" << left_right_channel_ << std::endl;
}

PwmMotorDriver::~PwmMotorDriver() {
    disconnect();
}

std::string PwmMotorDriver::getPwmBasePath(int chip) const {
    return "/sys/class/pwm/pwmchip" + std::to_string(chip) + "/";
}

bool PwmMotorDriver::connect() {
    std::cout << "正在初始化PWM电机驱动器..." << std::endl;
    std::cout << "前后控制: pwmchip" << front_back_chip_ << "/pwm" << front_back_channel_ << std::endl;
    std::cout << "左右转向: pwmchip" << left_right_chip_ << "/pwm" << left_right_channel_ << std::endl;
    std::cout << "周期: " << period_ns_ << "ns (" << (1000000000.0 / period_ns_) << "Hz)" << std::endl;
    std::cout << "占空比范围: " << duty_min_ns_ << "ns ~ " << duty_max_ns_ << "ns" << std::endl;
    std::cout << "中性位置: " << duty_neutral_ns_ << "ns" << std::endl;

    // 初始化前后通道
    if (front_back_channel_ >= 0 && front_back_channel_ < 4) {
        std::cout << "初始化前后控制通道..." << std::endl;

        if (!exportPwmChannel(front_back_chip_, front_back_channel_)) {
            std::cerr << "Failed to export front/back PWM channel "
                      << front_back_channel_ << " on pwmchip" << front_back_chip_ << std::endl;
            return false;
        }

        // 设置周期
        if (!setPeriod(front_back_chip_, front_back_channel_, period_ns_)) {
            std::cerr << "Failed to set period for front/back channel" << std::endl;
            return false;
        }

        // 设置极性（normal：占空比越高，信号越强）
        if (!setPolarity(front_back_chip_, front_back_channel_, "normal")) {
            std::cerr << "Warning: Failed to set polarity for front/back channel" << std::endl;
            // 极性设置失败不一定致命，继续执行
        }

        // 设置初始占空比为中性位置
        if (!setDutyCycle(front_back_chip_, front_back_channel_, duty_neutral_ns_)) {
            std::cerr << "Failed to set duty cycle for front/back channel" << std::endl;
            return false;
        }

        // 使能PWM
        if (!enablePwm(front_back_chip_, front_back_channel_, true)) {
            std::cerr << "Failed to enable front/back PWM channel" << std::endl;
            return false;
        }

        front_back_initialized_ = true;
        std::cout << "前后控制PWM通道初始化成功" << std::endl;
    }

    // 初始化左右转向通道
    if (left_right_channel_ >= 0 && left_right_channel_ < 4) {
        std::cout << "初始化左右转向通道..." << std::endl;

        if (!exportPwmChannel(left_right_chip_, left_right_channel_)) {
            std::cerr << "Failed to export left/right PWM channel "
                      << left_right_channel_ << " on pwmchip" << left_right_chip_ << std::endl;
            return false;
        }

        // 设置周期
        if (!setPeriod(left_right_chip_, left_right_channel_, period_ns_)) {
            std::cerr << "Failed to set period for left/right channel" << std::endl;
            return false;
        }

        // 设置极性
        if (!setPolarity(left_right_chip_, left_right_channel_, "normal")) {
            std::cerr << "Warning: Failed to set polarity for left/right channel" << std::endl;
        }

        // 设置初始占空比为中性位置
        if (!setDutyCycle(left_right_chip_, left_right_channel_, duty_neutral_ns_)) {
            std::cerr << "Failed to set duty cycle for left/right channel" << std::endl;
            return false;
        }

        // 使能PWM
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

    // 禁用前后通道
    if (front_back_initialized_) {
        std::cout << "关闭前后控制通道..." << std::endl;
        // 先设置占空比为中性位置
        setDutyCycle(front_back_chip_, front_back_channel_, duty_neutral_ns_);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 禁用PWM
        enablePwm(front_back_chip_, front_back_channel_, false);

        // 取消导出
        unexportPwmChannel(front_back_chip_, front_back_channel_);

        front_back_initialized_ = false;
    }

    // 禁用左右转向通道
    if (left_right_initialized_) {
        std::cout << "关闭左右转向通道..." << std::endl;
        // 先设置占空比为中性位置
        setDutyCycle(left_right_chip_, left_right_channel_, duty_neutral_ns_);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 禁用PWM
        enablePwm(left_right_chip_, left_right_channel_, false);

        // 取消导出
        unexportPwmChannel(left_right_chip_, left_right_channel_);

        left_right_initialized_ = false;
    }

    std::cout << "PWM电机驱动器已关闭" << std::endl;
}

void PwmMotorDriver::setMotorPercent(int motor_id, int percent) {
    // motor_id 是1-based，转换为0-based
    int channel = motor_id - 1;
    if (channel < 0 || channel >= 4) {
        std::cerr << "Invalid motor_id: " << motor_id << std::endl;
        return;
    }

    // 限制百分比范围
    percent = std::clamp(percent, -100, 100);

    // 根据通道号确定是前后还是转向
    if (channel == front_back_channel_) {
        front_back_duty_ = percent;
        applyChannelDuty(true);  // true = front/back
    } else if (channel == left_right_channel_) {
        left_right_duty_ = percent;
        applyChannelDuty(false);  // false = left/right
    } else {
        std::cerr << "Motor ID " << motor_id << " not mapped to any PWM channel" << std::endl;
    }
}

void PwmMotorDriver::setFrontBackPercent(int percent) {
    if (front_back_channel_ >= 0 && front_back_channel_ < 4) {
        front_back_duty_ = std::clamp(percent, -100, 100);
        applyChannelDuty(true);

        const char* direction = (percent > 0) ? "前进" : (percent < 0 ? "后退" : "停止");
        std::cout << "PWM前后控制: " << percent << "% (" << direction << ")" << std::endl;
    }
}

void PwmMotorDriver::setLeftRightPercent(int percent) {
    if (left_right_channel_ >= 0 && left_right_channel_ < 4) {
        left_right_duty_ = std::clamp(percent, -100, 100);
        applyChannelDuty(false);

        const char* direction = (percent > 0) ? "右转" : (percent < 0 ? "左转" : "停止");
        std::cout << "PWM转向控制: " << percent << "% (" << direction << ")" << std::endl;
    }
}

bool PwmMotorDriver::exportPwmChannel(int chip, int channel) {
    std::string base_path = getPwmBasePath(chip);
    std::string export_path = base_path + "export";
    std::string channel_str = std::to_string(channel);

    std::cout << "导出PWM通道 pwmchip" << chip << "/pwm" << channel << "..." << std::endl;

    // 检查是否已经导出
    std::string channel_path = base_path + "pwm" + std::to_string(channel);
    struct stat st;
    if (stat(channel_path.c_str(), &st) == 0) {
        std::cout << "PWM通道 pwmchip" << chip << "/pwm" << channel << " 已经导出" << std::endl;
        return true;
    }

    // 导出通道
    if (!writeSysfs(export_path, channel_str)) {
        std::cerr << "Failed to export PWM channel " << channel << " on pwmchip" << chip << std::endl;
        return false;
    }

    // 等待sysfs文件生成
    int retry = 0;
    while (retry < 10) {
        if (stat(channel_path.c_str(), &st) == 0) {
            std::cout << "PWM通道 pwmchip" << chip << "/pwm" << channel << " 导出成功" << std::endl;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retry++;
    }

    std::cerr << "Timeout waiting for PWM channel " << channel << " sysfs files on pwmchip" << chip << std::endl;
    return false;
}

void PwmMotorDriver::unexportPwmChannel(int chip, int channel) {
    std::string base_path = getPwmBasePath(chip);
    std::string unexport_path = base_path + "unexport";
    std::string channel_str = std::to_string(channel);

    std::cout << "取消导出PWM通道 pwmchip" << chip << "/pwm" << channel << std::endl;
    writeSysfs(unexport_path, channel_str);
}

bool PwmMotorDriver::setPeriod(int chip, int channel, uint64_t period_ns) {
    std::string base_path = getPwmBasePath(chip);
    std::string path = base_path + "pwm" + std::to_string(channel) + "/period";
    std::string period_str = std::to_string(period_ns);

    std::cout << "设置PWM通道 pwmchip" << chip << "/pwm" << channel << " 周期: " << period_str << "ns" << std::endl;
    return writeSysfs(path, period_str);
}

bool PwmMotorDriver::setDutyCycle(int chip, int channel, uint64_t duty_ns) {
    std::string base_path = getPwmBasePath(chip);
    std::string path = base_path + "pwm" + std::to_string(channel) + "/duty_cycle";
    std::string duty_str = std::to_string(duty_ns);

    return writeSysfs(path, duty_str);
}

bool PwmMotorDriver::setPolarity(int chip, int channel, const std::string& polarity) {
    std::string base_path = getPwmBasePath(chip);
    std::string path = base_path + "pwm" + std::to_string(channel) + "/polarity";

    std::cout << "设置PWM通道 pwmchip" << chip << "/pwm" << channel << " 极性: " << polarity << std::endl;
    return writeSysfs(path, polarity);
}

bool PwmMotorDriver::enablePwm(int chip, int channel, bool enable) {
    std::string base_path = getPwmBasePath(chip);
    std::string path = base_path + "pwm" + std::to_string(channel) + "/enable";
    std::string enable_str = enable ? "1" : "0";

    std::cout << (enable ? "使能" : "禁用") << " PWM通道 pwmchip" << chip << "/pwm" << channel << std::endl;
    return writeSysfs(path, enable_str);
}

uint64_t PwmMotorDriver::percentToDutyNs(int percent) const {
    // 将百分比转换为占空比纳秒值
    // percent: -100 ~ 100
    // duty: duty_min_ns_ ~ duty_max_ns_

    double factor = percent / 100.0;  // -1.0 ~ 1.0

    if (factor > 0) {
        // 正向：中性 ~ 最大
        return duty_neutral_ns_ + static_cast<uint64_t>(factor * (duty_max_ns_ - duty_neutral_ns_));
    } else if (factor < 0) {
        // 反向：中性 ~ 最小（factor为负数，用减法）
        return duty_neutral_ns_ - static_cast<uint64_t>((-factor) * (duty_neutral_ns_ - duty_min_ns_));
    } else {
        // 中性
        return duty_neutral_ns_;
    }
}

bool PwmMotorDriver::applyChannelDuty(bool is_front_back) {
    // 直接根据参数确定操作哪个PWM，不靠channel号判断
    int chip;
    int channel;
    int duty_percent;

    if (is_front_back) {
        if (!front_back_initialized_) return false;
        chip = front_back_chip_;
        channel = front_back_channel_;
        duty_percent = front_back_duty_;
    } else {
        if (!left_right_initialized_) return false;
        chip = left_right_chip_;
        channel = left_right_channel_;
        duty_percent = left_right_duty_;
    }

    uint64_t duty_ns = percentToDutyNs(duty_percent);

    std::cout << "PWM通道 pwmchip" << chip << "/pwm" << channel << ": "
              << duty_percent << "% -> "
              << duty_ns << "ns" << std::endl;

    return setDutyCycle(chip, channel, duty_ns);
}

bool PwmMotorDriver::writeSysfs(const std::string& path, const std::string& value) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open sysfs file: " << path << std::endl;
        return false;
    }

    file << value;
    if (file.fail()) {
        std::cerr << "Failed to write to sysfs file: " << path << " value: " << value << std::endl;
        return false;
    }

    file.close();
    return true;
}

std::string PwmMotorDriver::readSysfs(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open sysfs file for reading: " << path << std::endl;
        return "";
    }

    std::string value;
    std::getline(file, value);
    file.close();

    return value;
}
