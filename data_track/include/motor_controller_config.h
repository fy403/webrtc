#ifndef MOTOR_CONTROLLER_CONFIG_H
#define MOTOR_CONTROLLER_CONFIG_H

#include <string>
#include <cstdint>

/**
 * MotorController 配置类
 * 用于集中管理 MotorController 及其子组件（MotorDriver）的配置参数
 */
class MotorControllerConfig {
public:
    // ========== 通用配置参数 ==========
    // 电机驱动串口设备路径（例如：/dev/ttyUSB0）
    std::string motor_driver_port = "/dev/ttyUSB0";
    // 电机驱动类型（例如：uart,crsf 等）
    std::string motor_driver_type = "uart";
    // 后退时是否反转转向方向（默认：true）
    bool reverse_turn_when_backward = true;


    // ========== UART MotorDriver 配置参数 ==========
    // 电机驱动串口波特率
    int motor_driver_baudrate = 115200;
    // PWM 量程配置
    int16_t motor_pwm_forward_max = 3500;
    int16_t motor_pwm_reverse_max = -3500;
    int16_t motor_pwm_neutral = 0;
    // 电机通道配置（1~4）
    int motor_front_back_id = 2; // 前进/后退
    int motor_left_right_id = 4; // 左右转向


    // ========== CRSF Motor Driver 配置参数 ==========
    // 舵机配置（用于控制方向/左右转向）
    uint16_t crsf_servo_min_pulse = 500;      // 舵机最小脉冲宽度（微秒）
    uint16_t crsf_servo_max_pulse = 2500;     // 舵机最大脉冲宽度（微秒）
    uint16_t crsf_servo_neutral_pulse = 1500; // 舵机中位脉冲宽度（微秒）
    float crsf_servo_min_angle = 0.0f;        // 舵机最小角度（度）
    float crsf_servo_max_angle = 180.0f;      // 舵机最大角度（度）
    uint8_t crsf_servo_channel = 2;           // CRSF 舵机通道编号（1-16）

    // 电调配置（用于控制前后/前进后退）
    uint16_t crsf_esc_min_pulse = 900;        // 电调最小脉冲宽度（微秒）
    uint16_t crsf_esc_max_pulse = 2100;       // 电调最大脉冲宽度（微秒）
    uint16_t crsf_esc_neutral_pulse = 1500;   // 电调中位脉冲宽度（微秒）
    bool crsf_esc_reversible = true;          // 电调是否支持倒转
    uint8_t crsf_esc_channel = 1;             // CRSF 电调通道编号（1-16）

    /**
     * 构造函数 - 使用默认值
     */
    MotorControllerConfig() = default;

    /**
     * 构造函数 - 完整参数
     * @param port MotorDriver 使用的串口设备
     * @param driver_type MotorDriver 使用的驱动类型
     * @param baudrate MotorDriver 串口波特率
     */
    MotorControllerConfig(const std::string &port,
                          const std::string &driver_type,
                          int baudrate,
                          int16_t forward_max = 3500,
                          int16_t reverse_max = -3500,
                          int16_t neutral = 0,
                          int front_back_id = 2,
                          int left_right_id = 4)
            : motor_driver_port(port),
              motor_driver_type(driver_type),
              motor_driver_baudrate(baudrate),
              motor_pwm_forward_max(forward_max),
              motor_pwm_reverse_max(reverse_max),
              motor_pwm_neutral(neutral),
              motor_front_back_id(front_back_id),
              motor_left_right_id(left_right_id) {}
};

#endif // MOTOR_CONTROLLER_CONFIG_H

