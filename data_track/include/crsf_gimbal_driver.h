#ifndef CRSF_GIMBAL_DRIVER_H
#define CRSF_GIMBAL_DRIVER_H

#include "crsf_transport.h"
#include "crsf_motor_driver.h"   // 复用 ServoConfig, CRSFConfig
#include <memory>
#include <cstdint>
#include <algorithm>

/**
 * CRSFGimbalDriver - CRSF 协议云台驱动
 *
 * 通过共享的 CRSFTransport 控制云台舵机：
 * - 通道3（tilt）：俯仰轴
 * - 通道4（pan）：水平轴
 *
 * 提供角度控制（setTilt/setPan）和百分比控制（setTiltPercent/setPanPercent）
 */
class CRSFGimbalDriver {
public:
    /**
     * 构造函数
     * @param transport 共享的 CRSF 传输层实例
     * @param tilt_min_pulse 俯仰舵机最小脉冲宽度（微秒）
     * @param tilt_max_pulse 俯仰舵机最大脉冲宽度（微秒）
     * @param tilt_neutral_pulse 俯仰舵机中位脉冲宽度（微秒）
     * @param tilt_min_angle 俯仰最小角度（度）
     * @param tilt_max_angle 俯仰最大角度（度）
     * @param tilt_channel 俯仰 CRSF 通道编号（默认3）
     * @param pan_min_pulse 水平舵机最小脉冲宽度（微秒）
     * @param pan_max_pulse 水平舵机最大脉冲宽度（微秒）
     * @param pan_neutral_pulse 水平舵机中位脉冲宽度（微秒）
     * @param pan_min_angle 水平最小角度（度）
     * @param pan_max_angle 水平最大角度（度）
     * @param pan_channel 水平 CRSF 通道编号（默认4）
     */
    CRSFGimbalDriver(std::shared_ptr<CRSFTransport> transport,
                     uint16_t tilt_min_pulse = 900,
                     uint16_t tilt_max_pulse = 2100,
                     uint16_t tilt_neutral_pulse = 1500,
                     float tilt_min_angle = -90.0f,
                     float tilt_max_angle = 90.0f,
                     uint8_t tilt_channel = 3,
                     uint16_t pan_min_pulse = 900,
                     uint16_t pan_max_pulse = 2100,
                     uint16_t pan_neutral_pulse = 1500,
                     float pan_min_angle = -90.0f,
                     float pan_max_angle = 90.0f,
                     uint8_t pan_channel = 4);

    ~CRSFGimbalDriver();

    // === 角度控制接口（单位：度）===

    /**
     * 设置俯仰角度
     * @param angle_deg 角度（度），自动钳位到 [tilt_min_angle, tilt_max_angle]
     */
    void setTilt(float angle_deg);

    /**
     * 设置水平角度
     * @param angle_deg 角度（度），自动钳位到 [pan_min_angle, pan_max_angle]
     */
    void setPan(float angle_deg);

    // === 百分比控制接口（-100~+100，适配 ControlFrame float channel）===

    /**
     * 设置俯仰百分比
     * @param percent 百分比（-100~+100），0=中位
     */
    void setTiltPercent(int percent);

    /**
     * 设置水平百分比
     * @param percent 百分比（-100~+100），0=中位
     */
    void setPanPercent(int percent);

    // === 辅助接口 ===

    /**
     * 所有轴回到中位
     */
    void centerAll();

private:
    std::shared_ptr<CRSFTransport> transport_;
    std::unique_ptr<ServoConfig> tilt_config_;
    std::unique_ptr<ServoConfig> pan_config_;
    std::unique_ptr<CRSFConfig> crsf_config_;

    /**
     * 角度转 PWM 脉宽
     */
    uint16_t angleToPWM(float angle, const ServoConfig& config);

    /**
     * PWM 脉宽转 CRSF 通道原始值
     */
    uint16_t pwmToChannel(uint16_t pwm_us);

    /**
     * 写入指定通道的 PWM 值
     */
    void setChannelPWM(uint8_t ch_idx, uint16_t pwm_us);
};

#endif // CRSF_GIMBAL_DRIVER_H
