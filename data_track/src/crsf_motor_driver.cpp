#include "crsf_motor_driver.h"
#include <iostream>
#include <algorithm>
#include <cmath>

CRSFMotorDriver::CRSFMotorDriver(std::shared_ptr<CRSFTransport> transport,
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
    : transport_(std::move(transport)),
      servo_config_(std::make_unique<ServoConfig>(
          servo_min_pulse, servo_max_pulse, servo_neutral_pulse,
          servo_min_angle, servo_max_angle, servo_channel,
          "CRSF Servo")),
      esc_config_(std::make_unique<ESCConfig>(
          esc_min_pulse, esc_max_pulse, esc_neutral_pulse,
          esc_reversible, esc_channel, "CRSF ESC")),
      crsf_config_(std::make_unique<CRSFConfig>()) {
}

CRSFMotorDriver::~CRSFMotorDriver() {
    disconnect();
}

bool CRSFMotorDriver::connect() {
    // Transport 生命周期由 MotorController 管理，这里无需操作
    return (transport_ != nullptr);
}

void CRSFMotorDriver::disconnect() {
    // Transport 生命周期由 MotorController 管理，这里无需操作
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
    if (transport_) {
        transport_->setChannel(ch_idx, escPwmToChannel(pwm_us));
    }
}

void CRSFMotorDriver::setServoAngle(float angle) {
    if (angle < servo_config_->min_angle)
        angle = servo_config_->min_angle;
    if (angle > servo_config_->max_angle)
        angle = servo_config_->max_angle;

    uint16_t pwm_us = angleToPWM(angle);
    uint8_t ch_idx = servo_config_->channel - 1;
    if (transport_) {
        transport_->setChannel(ch_idx, servoPwmToChannel(pwm_us));
    }
}

void CRSFMotorDriver::setMotorPercent(int motor_id, int percent) {
    // CRSF驱动中，motor_id 1 表示电调（前后），motor_id 2 表示舵机（左右）
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
    if (transport_) {
        transport_->setChannel(ch_idx, servoPwmToChannel(clamped_pwm));
    }
}
