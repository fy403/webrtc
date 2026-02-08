#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <string>
#include <chrono>
#include <thread>
#include "motor_driver.h"
#include "motor_controller_config.h"
#include "constants.h"
#include "rc_protocol_v2.h"

class MotorController {
public:
    /**
     * 构造函数 - 使用配置类
     * @param config MotorController 配置，包含 MotorDriver 的配置参数
     */
    explicit MotorController(const MotorControllerConfig &config = MotorControllerConfig());

    ~MotorController();

    void stopAll();

    void printStatus();

    void emergencyStop();

    void applyControl(const RCProtocolV2::ControlFrame &control_frame);

    void setFrontBackSpeed(int speed_percent);

    void setLeftRightSpeed(int speed_percent);

private:
    MotorControllerConfig config_;
    MotorDriver *motor_driver;
    int front_back_speed_{0};
    int left_right_speed_{0};
};

#endif // MOTOR_CONTROLLER_H