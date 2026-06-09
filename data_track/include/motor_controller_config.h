#ifndef MOTOR_CONTROLLER_CONFIG_H
#define MOTOR_CONTROLLER_CONFIG_H

#include <string>
#include <cstdint>

class MotorControllerConfig {
public:
    // ========== 通用 ==========
    std::string motor_driver_port = "/dev/ttyUSB0";
    std::string motor_driver_type = "crsf";
    int motor_driver_baudrate = 115200;

    // ========== CRSF 驱动 ==========
    uint16_t crsf_neutral_pwm = 1500;   // CRSF 协议默认中位值 (us)

    // ========== PWM 驱动 ==========
    uint16_t pwm_neutral_pwm = 1500;    // PWM 协议默认中位值 (us)

    // ========== PWM 驱动 ==========
    int pwm_front_back_chip = 0;
    int pwm_left_right_chip = 1;
    int pwm_front_back_channel = 0;
    int pwm_left_right_channel = 0;
    uint64_t pwm_period_ns = 20000000;

    // 协议通道索引：指定 frame.channels[] 中哪个索引对应前后/左右
    int pwm_proto_forward_idx = 0;   // 前后控制对应的协议通道索引（默认0）
    int pwm_proto_turn_idx = 1;      // 左右转向对应的协议通道索引（默认1）

    MotorControllerConfig() = default;

    MotorControllerConfig(const std::string &port,
                          const std::string &driver_type,
                          int baudrate)
        : motor_driver_port(port),
          motor_driver_type(driver_type),
          motor_driver_baudrate(baudrate) {
    }
};

#endif // MOTOR_CONTROLLER_CONFIG_H
