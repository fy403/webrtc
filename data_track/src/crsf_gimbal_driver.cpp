#include "crsf_gimbal_driver.h"
#include <iostream>
#include <algorithm>
#include <cmath>

CRSFGimbalDriver::CRSFGimbalDriver(std::shared_ptr<CRSFTransport> transport,
                                   uint16_t tilt_min_pulse,
                                   uint16_t tilt_max_pulse,
                                   uint16_t tilt_neutral_pulse,
                                   float tilt_min_angle,
                                   float tilt_max_angle,
                                   uint8_t tilt_channel,
                                   uint16_t pan_min_pulse,
                                   uint16_t pan_max_pulse,
                                   uint16_t pan_neutral_pulse,
                                   float pan_min_angle,
                                   float pan_max_angle,
                                   uint8_t pan_channel)
    : transport_(std::move(transport)),
      tilt_config_(std::make_unique<ServoConfig>(
          tilt_min_pulse, tilt_max_pulse, tilt_neutral_pulse,
          tilt_min_angle, tilt_max_angle, tilt_channel,
          "CRSF Gimbal Tilt")),
      pan_config_(std::make_unique<ServoConfig>(
          pan_min_pulse, pan_max_pulse, pan_neutral_pulse,
          pan_min_angle, pan_max_angle, pan_channel,
          "CRSF Gimbal Pan")),
      crsf_config_(std::make_unique<CRSFConfig>()) {
}

CRSFGimbalDriver::~CRSFGimbalDriver() {
    // Transport 生命周期由 MotorController 管理
}

uint16_t CRSFGimbalDriver::angleToPWM(float angle, const ServoConfig& config) {
    float angle_range = config.max_angle - config.min_angle;
    if (std::abs(angle_range) < 0.001f) {
        return config.neutral_pulse_width;
    }
    float ratio = (angle - config.min_angle) / angle_range;
    return static_cast<uint16_t>(config.min_pulse_width +
                                 ratio * (config.max_pulse_width - config.min_pulse_width));
}

uint16_t CRSFGimbalDriver::pwmToChannel(uint16_t pwm_us) {
    // 使用 CRSF 配置进行 PWM → 通道值转换（标准映射：900-2100us → 172-1811）
    // 但这里需要根据实际配置的脉冲范围来映射
    float ratio = 0.5f;  // 默认中位
    // 注意：pwmToChannel 在这里用于通用转换，默认使用标准 CRSF 通道映射
    return static_cast<uint16_t>(crsf_config_->channel_min +
                                 ratio * (crsf_config_->channel_max - crsf_config_->channel_min));
}

void CRSFGimbalDriver::setChannelPWM(uint8_t ch_idx, uint16_t pwm_us) {
    if (!transport_) return;

    // PWM → CRSF channel value
    // 使用 SG90 实际脉冲范围 500-2500us 映射到 CRSF 172-1811
    constexpr uint16_t PWM_MIN = 500;
    constexpr uint16_t PWM_MAX = 2500;

    uint16_t clamped = std::max(PWM_MIN, std::min(PWM_MAX, pwm_us));
    float ratio = static_cast<float>(clamped - PWM_MIN) / (PWM_MAX - PWM_MIN);
    uint16_t channel_value = static_cast<uint16_t>(
        crsf_config_->channel_min +
        ratio * (crsf_config_->channel_max - crsf_config_->channel_min));

    transport_->setChannel(ch_idx, channel_value);
}

void CRSFGimbalDriver::setTilt(float angle_deg) {
    if (angle_deg < tilt_config_->min_angle)
        angle_deg = tilt_config_->min_angle;
    if (angle_deg > tilt_config_->max_angle)
        angle_deg = tilt_config_->max_angle;

    uint16_t pwm_us = angleToPWM(angle_deg, *tilt_config_);
    uint8_t ch_idx = tilt_config_->channel - 1;
    setChannelPWM(ch_idx, pwm_us);
}

void CRSFGimbalDriver::setPan(float angle_deg) {
    if (angle_deg < pan_config_->min_angle)
        angle_deg = pan_config_->min_angle;
    if (angle_deg > pan_config_->max_angle)
        angle_deg = pan_config_->max_angle;

    uint16_t pwm_us = angleToPWM(angle_deg, *pan_config_);
    uint8_t ch_idx = pan_config_->channel - 1;
    setChannelPWM(ch_idx, pwm_us);
}

void CRSFGimbalDriver::setTiltPercent(int percent) {
    int clamped = std::max(-100, std::min(100, percent));

    float min_angle = tilt_config_->min_angle;
    float max_angle = tilt_config_->max_angle;
    float center_angle = (min_angle + max_angle) / 2.0f;
    float angle_range = max_angle - min_angle;

    float angle = center_angle + (clamped / 100.0f) * (angle_range / 2.0f);
    setTilt(angle);
}

void CRSFGimbalDriver::setPanPercent(int percent) {
    int clamped = std::max(-100, std::min(100, percent));

    float min_angle = pan_config_->min_angle;
    float max_angle = pan_config_->max_angle;
    float center_angle = (min_angle + max_angle) / 2.0f;
    float angle_range = max_angle - min_angle;

    float angle = center_angle + (clamped / 100.0f) * (angle_range / 2.0f);
    setPan(angle);
}

void CRSFGimbalDriver::centerAll() {
    setTiltPercent(0);
    setPanPercent(0);
}
