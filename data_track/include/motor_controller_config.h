#ifndef MOTOR_CONTROLLER_CONFIG_H
#define MOTOR_CONTROLLER_CONFIG_H

#include <string>

/**
 * MotorController 配置类
 * 用于集中管理 MotorController 及其子组件（MotorDriver）的配置参数
 */
class MotorControllerConfig {
public:
    // ========== MotorDriver 配置参数 ==========
    // 电机驱动串口设备路径（例如：/dev/ttyUSB0）
    std::string motor_driver_port = "/dev/ttyUSB0";
    
    // 电机驱动类型（例如：uart, can, i2c 等）
    std::string motor_driver_type = "uart";
    
    // 电机驱动串口波特率
    int motor_driver_baudrate = 115200;

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
                          int baudrate)
        : motor_driver_port(port),
          motor_driver_type(driver_type),
          motor_driver_baudrate(baudrate) {}
};

#endif // MOTOR_CONTROLLER_CONFIG_H

