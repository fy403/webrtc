#ifndef PWM_MOTOR_DRIVER_H
#define PWM_MOTOR_DRIVER_H

#include "motor_driver.h"
#include <string>
#include <iostream>
#include <fstream>

/**
 * PWM电机驱动器类
 * 使用Linux PWM sysfs接口控制Radxa Zero 3W的PWM引脚
 * 
 * Radxa Zero 3W可用PWM引脚：
 * - PWM8_M0:  Pin 15 (GPIO3_B0) - 默认前后控制
 * - PWM9_M0:  Pin 18 (GPIO3_B2) - 默认左右转向
 * - PWM14_M0: Pin 7  (GPIO3_C4)
 * - PWM15_IR_M1: Pin 19 (GPIO4_C3)
 * - PWM14_M1: Pin 22 (GPIO4_C2)
 */
class PwmMotorDriver : public MotorDriver {
public:
    /**
     * 构造函数
     * @param front_back_chip 前后控制PWM芯片编号（例如：0, 1, 2...）
     * @param left_right_chip 左右转向PWM芯片编号
     * @param period_ns PWM周期（纳秒），默认20000000ns = 50Hz（适合舵机/电调）
     * @param duty_min_ns 最小占空比（纳秒），对应反向最大
     * @param duty_max_ns 最大占空比（纳秒），对应正向最大
     * @param duty_neutral_ns 中性位置占空比（纳秒），对应停止
     * @param front_back_channel 前后控制通道 (0-3, -1表示不使用)
     * @param left_right_channel 左右转向通道 (0-3, -1表示不使用)
     */
    PwmMotorDriver(int front_back_chip = 0,
                   int left_right_chip = 1,
                   uint64_t period_ns = 20000000,
                   uint64_t duty_min_ns = 1000000,
                   uint64_t duty_max_ns = 2000000,
                   uint64_t duty_neutral_ns = 1500000,
                   int front_back_channel = 0,
                   int left_right_channel = 0);

    ~PwmMotorDriver() override;

    // 实现纯虚函数
    bool connect() override;
    void disconnect() override;
    void setMotorPercent(int motor_id, int percent) override;
    void setFrontBackPercent(int percent) override;
    void setLeftRightPercent(int percent) override;

private:
    int front_back_chip_;        // 前后控制PWM芯片编号
    int left_right_chip_;        // 左右转向PWM芯片编号
    uint64_t period_ns_;        // PWM周期（纳秒）
    uint64_t duty_min_ns_;      // 最小占空比（反向最大）
    uint64_t duty_max_ns_;      // 最大占空比（正向最大）
    uint64_t duty_neutral_ns_;  // 中性位置占空比（停止）
    int front_back_channel_;    // 前后控制通道
    int left_right_channel_;    // 左右转向通道
    
    // 当前占空比
    int front_back_duty_;      // 前后控制占空比 (-100 ~ 100)
    int left_right_duty_;      // 左右转向占空比 (-100 ~ 100)
    
    // 初始化标志
    bool front_back_initialized_;   // 前后控制初始化标志
    bool left_right_initialized_;   // 左右转向初始化标志

    // 私有方法
    bool exportPwmChannel(int chip, int channel);
    void unexportPwmChannel(int chip, int channel);
    bool setPeriod(int chip, int channel, uint64_t period_ns);
    bool setDutyCycle(int chip, int channel, uint64_t duty_ns);
    bool setPolarity(int chip, int channel, const std::string& polarity);
    bool enablePwm(int chip, int channel, bool enable);
    std::string getPwmBasePath(int chip) const;
    
    // 将百分比转换为占空比纳秒值
    uint64_t percentToDutyNs(int percent) const;
    
    // 应用指定通道的占空比
    // is_front_back: true=前后控制, false=左右转向
    bool applyChannelDuty(bool is_front_back);
    
    // 写入sysfs文件
    static bool writeSysfs(const std::string& path, const std::string& value);
    
    // 读取sysfs文件
    static std::string readSysfs(const std::string& path);
};

#endif // PWM_MOTOR_DRIVER_H
