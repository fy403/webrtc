#ifndef CRSF_MOTOR_DRIVER_H
#define CRSF_MOTOR_DRIVER_H

#include "motor_driver.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <vector>

// 前向声明
class ServoConfig;
class ESCConfig;
class CRSFConfig;
class SerialPort;

/**
 * CRSF Motor Driver
 * 基于 CRSF 协议的电机驱动实现
 * - 使用舵机控制方向（左右转向）
 * - 使用电调控制前后（前进/后退）
 */
class CRSFMotorDriver : public MotorDriver {
public:
    /**
     * 构造函数
     * @param port CRSF 串口设备路径
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
    CRSFMotorDriver(const std::string &port,
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
    bool connect() override;
    void disconnect() override;
    void setMotorPercent(int motor_id, int percent) override;
    void setFrontBackPercent(int percent) override;
    void setLeftRightPercent(int percent) override;

private:
    std::string port_;
    std::unique_ptr<SerialPort> serial_;
    std::unique_ptr<ServoConfig> servo_config_;
    std::unique_ptr<ESCConfig> esc_config_;
    std::unique_ptr<CRSFConfig> crsf_config_;
    
    std::vector<uint16_t> channels_;
    std::thread send_thread_;
    std::atomic<bool> running_;
    std::mutex channels_mutex_;

    // CRSF 协议相关方法
    uint8_t calculateCRC8(const uint8_t *data, size_t length);
    void packChannels11bit(uint8_t *buffer, const std::vector<uint16_t> &ch_values, int num_channels);
    void createChannelsFrame(uint8_t *buffer, size_t &length);
    void sendThreadFunc();
    
    // 转换方法
    uint16_t servoPwmToChannel(uint16_t pwm_us);
    uint16_t escPwmToChannel(uint16_t pwm_us);
    uint16_t angleToPWM(float angle);
    void setThrottlePWM(uint16_t pwm_us);
    void setServoAngle(float angle);
};

#endif // CRSF_MOTOR_DRIVER_H

