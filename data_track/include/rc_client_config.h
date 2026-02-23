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

    // GPS模块串口设备路径（例如：/dev/ttyUSB0），空字符串表示不使用GPS模块
    std::string system_monitor_gps_port = "";

    // GPS模块串口波特率（仅在 system_monitor_gps_port 不为空时有效）
    int system_monitor_gps_baudrate = 9600;
    
    // ========== 失控保护配置参数 ==========
    // Watchdog超时时间（毫秒），如果超过此时间未收到控制命令，自动停止电机
    // 默认值：300ms，建议范围：200-500ms
    int watchdog_timeout_ms = 300;
    
    // 检查是否配置了4G模块
    bool has4gConfig() const { return !system_monitor_gsm_port.empty(); }

    // 检查是否配置了GPS模块
    bool hasGpsConfig() const { return !system_monitor_gps_port.empty(); }

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
     * @param gps_port SystemMonitor 使用的GPS模块串口设备（可选，空字符串表示不使用）
     * @param gps_baudrate SystemMonitor 使用的GPS模块串口波特率（仅在 gps_port 不为空时有效）
     */
    RCClientConfig(const std::string &motor_port,
                   const std::string &motor_driver_type,
                   int motor_baudrate,
                   const std::string &gsm_port = "",
                   int gsm_baudrate = 115200,
                   const std::string &gps_port = "",
                   int gps_baudrate = 9600)
        : motor_controller_config(motor_port, motor_driver_type, motor_baudrate),
          system_monitor_gsm_port(gsm_port),
          system_monitor_gsm_baudrate(gsm_baudrate),
          system_monitor_gps_port(gps_port),
          system_monitor_gps_baudrate(gps_baudrate) {}
};

#endif // RC_CLIENT_CONFIG_H

