#ifndef RC_CLIENT_CONFIG_H
#define RC_CLIENT_CONFIG_H

#include <string>
#include "motor_controller_config.h"

/**
 * RCClient 系统配置类
 * 用于集中管理 RCClient 及其子组件的配置参数
 */
class RCClientConfig {
public:
    // ========== MotorController 配置 ==========
    // MotorController 及其子组件（MotorDriver）的配置
    MotorControllerConfig motor_controller_config;

    // ========== SystemMonitor 配置参数（可选）==========
    // 4G模块串口设备路径（例如：/dev/ttyACM0），空字符串表示不使用4G模块
    std::string system_monitor_gsm_port = "";
    
    // 4G模块串口波特率（仅在 system_monitor_gsm_port 不为空时有效）
    int system_monitor_gsm_baudrate = 115200;
    
    // 检查是否配置了4G模块
    bool has4gConfig() const { return !system_monitor_gsm_port.empty(); }

    /**
     * 构造函数 - 使用默认值
     */
    RCClientConfig() = default;

    /**
     * 构造函数 - 完整参数
     * @param motor_port MotorController 的 MotorDriver 使用的串口设备
     * @param motor_driver_type MotorController 的 MotorDriver 使用的驱动类型
     * @param motor_baudrate MotorController 的 MotorDriver 串口波特率
     * @param gsm_port SystemMonitor 使用的4G模块串口设备（可选，空字符串表示不使用）
     * @param gsm_baudrate SystemMonitor 使用的4G模块串口波特率（仅在 gsm_port 不为空时有效）
     */
    RCClientConfig(const std::string &motor_port,
                   const std::string &motor_driver_type,
                   int motor_baudrate,
                   const std::string &gsm_port = "",
                   int gsm_baudrate = 115200)
        : motor_controller_config(motor_port, motor_driver_type, motor_baudrate),
          system_monitor_gsm_port(gsm_port),
          system_monitor_gsm_baudrate(gsm_baudrate) {}
};

#endif // RC_CLIENT_CONFIG_H

