#ifndef CRSF_MOTOR_DRIVER_H
#define CRSF_MOTOR_DRIVER_H

#include "motor_driver.h"
#include "crsf_transport.h"
#include <string>
#include <memory>
#include <cstdint>
#include <cmath>

/**
 * 舵机配置类
 * 用于配置不同类型的舵机参数
 */
class ServoConfig
{
public:
    // 舵机最小脉冲宽度 (单位: 微秒)
    uint16_t min_pulse_width;

    // 舵机最大脉冲宽度 (单位: 微秒)
    uint16_t max_pulse_width;

    // 舵机中位脉冲宽度 (单位: 微秒)
    uint16_t neutral_pulse_width;

    // 舵机最小角度
    float min_angle;

    // 舵机最大角度
    float max_angle;

    // 舵机名称
    std::string name;

    // 接CRSF通道编号
    uint8_t channel;

    /**
     * 默认构造函数 - 使用标准舵机参数 (900-2100us, 0-180度)
     */
    ServoConfig() : min_pulse_width(900),
                    max_pulse_width(2100),
                    neutral_pulse_width(1500),
                    min_angle(0.0f),
                    max_angle(180.0f),
                    channel(0),
                    name("Standard Servo") {}

    /**
     * 自定义构造函数
     * @param min_pw 最小脉冲宽度
     * @param max_pw 最大脉冲宽度
     * @param neutral_pw 中位脉冲宽度
     * @param min_ang 最小角度
     * @param max_ang 最大角度
     * @param servo_name 舵机名称
     */
    ServoConfig(uint16_t min_pw, uint16_t max_pw, uint16_t neutral_pw,
                float min_ang, float max_ang, uint8_t servo_channel, const std::string &servo_name) : min_pulse_width(min_pw),
                                                                                                      max_pulse_width(max_pw),
                                                                                                      neutral_pulse_width(neutral_pw),
                                                                                                      min_angle(min_ang),
                                                                                                      max_angle(max_ang),
                                                                                                      channel(servo_channel),
                                                                                                      name(servo_name) {}
};

/**
 * 电调配置类
 * 用于配置不同类型的电调参数
 */
class ESCConfig
{
public:
    // 电调最小脉冲宽度 (单位: 微秒)
    uint16_t min_pulse_width;

    // 电调最大脉冲宽度 (单位: 微秒)
    uint16_t max_pulse_width;

    // 电调中位/停止脉冲宽度 (单位: 微秒)
    uint16_t neutral_pulse_width;

    // 是否支持倒转 (反向驱动)
    bool reversible;

    // 电调名称
    std::string name;

    // CRSF通道编号
    uint8_t channel;

    /**
     * 默认构造函数 - 使用标准电调参数 (1000-2000us)
     */
    ESCConfig() : min_pulse_width(1000),
                  max_pulse_width(2000),
                  neutral_pulse_width(1500),
                  reversible(false),
                  channel(1),
                  name("Standard ESC") {}

    /**
     * 自定义构造函数
     * @param min_pw 最小脉冲宽度
     * @param max_pw 最大脉冲宽度
     * @param neutral_pw 中位脉冲宽度
     * @param is_reversible 是否支持倒转
     * @param esc_name 电调名称
     */
    ESCConfig(uint16_t min_pw, uint16_t max_pw, uint16_t neutral_pw,
              bool is_reversible, uint8_t esc_channel, const std::string &esc_name) : min_pulse_width(min_pw),
                                                                                      max_pulse_width(max_pw),
                                                                                      neutral_pulse_width(neutral_pw),
                                                                                      reversible(is_reversible),
                                                                                      channel(esc_channel),
                                                                                      name(esc_name) {}
};

/**
 * CRSF协议配置类
 * 用于配置CRSF协议相关参数
 */
class CRSFConfig
{
public:
    // CRSF协议同步字节
    uint8_t sync_byte;

    // CRSF通道最小值
    uint16_t channel_min;

    // CRSF通道中位值
    uint16_t channel_neutral;

    // CRSF通道最大值
    uint16_t channel_max;

    // CRSF通道数量
    uint8_t channel_count;

    /**
     * 默认构造函数 - 使用标准CRSF参数
     */
    CRSFConfig() : sync_byte(0xC8),
                   channel_min(172),
                   channel_neutral(992),
                   channel_max(1811),
                   channel_count(12) {}

    /**
     * 自定义构造函数
     * @param sync 同步字节
     * @param ch_min 通道最小值
     * @param ch_neutral 通道中位值
     * @param ch_max 通道最大值
     * @param ch_count 通道数量
     */
    CRSFConfig(uint8_t sync, uint16_t ch_min, uint16_t ch_neutral, uint16_t ch_max, uint8_t ch_count) : sync_byte(sync),
                                                                                                        channel_min(ch_min),
                                                                                                        channel_neutral(ch_neutral),
                                                                                                        channel_max(ch_max),
                                                                                                        channel_count(ch_count) {}
};

/**
 * CRSF Motor Driver
 * 基于 CRSF 协议的电机驱动实现
 * - 使用舵机控制方向（左右转向）
 * - 使用电调控制前后（前进/后退）
 * 
 * 注意：不再拥有串口，通过 shared_ptr<CRSFTransport> 共享底层通信层
 */
class CRSFMotorDriver : public MotorDriver
{
public:
    /**
     * 构造函数
     * @param transport 共享的 CRSF 传输层实例
     * @param servo_min_pulse 舵机最小脉冲宽度（微秒）
     * @param servo_max_pulse 舵机最大脉冲宽度（微秒）
     * @param servo_neutral_pulse 舵机中位脉冲宽度（微秒）
     * @param servo_min_angle 舵机最小角度（度）
     * @param servo_max_angle 舵机最大角度（度）
     * @param servo_channel CRSF 舵机通道编号（1-16）
     * @param esc_min_pulse 电调最小脉冲宽度（微秒）
     * @param esc_max_pulse 电调最大脉冲宽度（微秒）
     * @param esc_neutral_pulse 电调中位脉冲宽度（微秒）
     * @param esc_reversible 电调是否支持倒转
     * @param esc_channel CRSF 电调通道编号（1-16）
     */
    CRSFMotorDriver(std::shared_ptr<CRSFTransport> transport,
                    uint16_t servo_min_pulse = 500,
                    uint16_t servo_max_pulse = 2500,
                    uint16_t servo_neutral_pulse = 1500,
                    float servo_min_angle = 0.0f,
                    float servo_max_angle = 180.0f,
                    uint8_t servo_channel = 2,
                    uint16_t esc_min_pulse = 900,
                    uint16_t esc_max_pulse = 2100,
                    uint16_t esc_neutral_pulse = 1500,
                    bool esc_reversible = true,
                    uint8_t esc_channel = 1);

    ~CRSFMotorDriver();

    // 实现 MotorDriver 接口
    // connect/disconnect 为空操作，transport 生命周期由外部管理
    bool connect() override;
    void disconnect() override;
    void setMotorPercent(int motor_id, int percent) override;
    void setFrontBackPercent(int percent) override;
    void setLeftRightPercent(int percent) override;

private:
    std::shared_ptr<CRSFTransport> transport_;
    std::unique_ptr<ServoConfig> servo_config_;
    std::unique_ptr<ESCConfig> esc_config_;
    std::unique_ptr<CRSFConfig> crsf_config_;

    // 转换方法
    uint16_t servoPwmToChannel(uint16_t pwm_us);
    uint16_t escPwmToChannel(uint16_t pwm_us);
    uint16_t angleToPWM(float angle);
    void setThrottlePWM(uint16_t pwm_us);
    void setServoAngle(float angle);
    
    // 检查是否为转向舵机模式（而非转向马达模式）
    bool isSteeringServo();
    
    // 设置转向马达
    void setSteeringMotor(int percent);
};

#endif // CRSF_MOTOR_DRIVER_H