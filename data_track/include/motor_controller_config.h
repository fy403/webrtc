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
    // 电机驱动类型（例如：uart,crsf,pwm 等）
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
    uint16_t crsf_servo_min_pulse = 900; // 舵机最小脉冲宽度（微秒）
    uint16_t crsf_servo_max_pulse = 2100; // 舵机最大脉冲宽度（微秒）
    uint16_t crsf_servo_neutral_pulse = 1500; // 舵机中位脉冲宽度（微秒）
    float crsf_servo_min_angle = 0.0f; // 舵机最小角度（度）
    float crsf_servo_max_angle = 0.0f; // 舵机最大角度（度）
    uint8_t crsf_servo_channel = 2; // CRSF 舵机通道编号（1-16）

    // 电调配置（用于控制前后/前进后退）
    uint16_t crsf_esc_min_pulse = 900; // 电调最小脉冲宽度（微秒）
    uint16_t crsf_esc_max_pulse = 2100; // 电调最大脉冲宽度（微秒）
    uint16_t crsf_esc_neutral_pulse = 1510; // 电调中位脉冲宽度（微秒）
    bool crsf_esc_reversible = true; // 电调是否支持倒转
    uint8_t crsf_esc_channel = 1; // CRSF 电调通道编号（1-16）

    // ========== PWM Motor Driver 配置参数 ==========
    // PWM 芯片编号（例如：0, 1, 2...）
    // PWM8_M0 (Pin 15) 对应芯片编号（前后控制）
    int pwm_front_back_chip = 0;
    // PWM9_M0 (Pin 18) 对应芯片编号（左右转向）
    int pwm_left_right_chip = 1;
    // PWM 通道编号（每个 pwmchip 只有 1 个通道：通道 0）
    int pwm_front_back_channel = 0; // 前后控制通道（pwmchip0/pwm0 -> Pin 15）
    int pwm_left_right_channel = 0; // 左右转向通道（pwmchip1/pwm0 -> Pin 18）
    // PWM 周期（纳秒），默认20000000ns = 50Hz（适合舵机/电调）
    uint64_t pwm_period_ns = 20000000;
    // 占空比范围（纳秒），与CRSF配置保持一致（900~2100us）
    uint64_t pwm_duty_min_ns = 900000; // 最小占空比（反向最大），900us
    uint64_t pwm_duty_max_ns = 2100000; // 最大占空比（正向最大），2100us
    uint64_t pwm_duty_neutral_ns = 1500000; // 中性位置占空比（停止），1500us


    // ========== CRSF Gimbal Driver 配置参数 ==========
    // 是否启用云台控制（默认：false，保持向后兼容）
    bool enable_gimbal = false;

    // 俯仰（Tilt）通道配置
    uint8_t crsf_gimbal_tilt_channel = 3; // CRSF 俯仰通道编号（1-16）
    uint16_t crsf_gimbal_tilt_min_pulse = 900; // 俯仰舵机最小脉冲宽度（微秒）
    uint16_t crsf_gimbal_tilt_max_pulse = 2100; // 俯仰舵机最大脉冲宽度（微秒）
    uint16_t crsf_gimbal_tilt_neutral_pulse = 1500; // 俯仰舵机中位脉冲宽度（微秒）
    float crsf_gimbal_tilt_min_angle = -90.0f; // 俯仰最小角度（度）
    float crsf_gimbal_tilt_max_angle = 90.0f; // 俯仰最大角度（度）

    // 水平（Pan）通道配置
    uint8_t crsf_gimbal_pan_channel = 4; // CRSF 水平通道编号（1-16）
    uint16_t crsf_gimbal_pan_min_pulse = 900; // 水平舵机最小脉冲宽度（微秒）
    uint16_t crsf_gimbal_pan_max_pulse = 2100; // 水平舵机最大脉冲宽度（微秒）
    uint16_t crsf_gimbal_pan_neutral_pulse = 1500; // 水平舵机中位脉冲宽度（微秒）
    float crsf_gimbal_pan_min_angle = -90.0f; // 水平最小角度（度）
    float crsf_gimbal_pan_max_angle = 90.0f; // 水平最大角度（度）

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
          motor_left_right_id(left_right_id) {
    }
};

#endif // MOTOR_CONTROLLER_CONFIG_H
