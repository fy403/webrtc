#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <string>
#include <chrono>
#include <thread>
#include <memory>
#include "motor_driver.h"
#include "motor_controller_config.h"
#include "constants.h"
#include "rc_protocol_v2.h"

class CRSFTransport;
class CRSFGimbalDriver;

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

    /**
     * 应用控制帧
     * channels[0] → 电机前后, channels[1] → 电机左右
     * channels[2] → 云台俯仰, channels[3] → 云台水平（仅当 enable_gimbal=true）
     */
    void applyControl(const RCProtocolV2::ControlFrame &control_frame);

    void setFrontBackSpeed(int speed_percent);

    void setLeftRightSpeed(int speed_percent);

private:
    MotorControllerConfig config_;
    MotorDriver *motor_driver;

    // CRSF 模式下共享的传输层和云台驱动
    std::shared_ptr<CRSFTransport> crsf_transport_;
    std::shared_ptr<CRSFGimbalDriver> gimbal_driver_;

    int front_back_speed_{0};
    int left_right_speed_{0};
    int tilt_percent_{0};
    int pan_percent_{0};
};

#endif // MOTOR_CONTROLLER_H