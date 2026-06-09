#ifndef DUMMY_MOTOR_DRIVER_H
#define DUMMY_MOTOR_DRIVER_H

#include "motor_driver.h"
#include <string>

/**
 * Dummy Motor Driver
 * 用于调试和测试，接受 peer 信号并打印，不执行实际硬件操作
 */
class DummyMotorDriver : public MotorDriver {
public:
    DummyMotorDriver(const std::string &name = "DummyMotor");
    
    ~DummyMotorDriver() override;

    bool connect() override;
    void disconnect() override;
    void applyControl(const RCProtocolV2::ControlFrame& frame) override;
    void stopAll() override;

private:
    std::string name_;
    bool connected_;
};

#endif // DUMMY_MOTOR_DRIVER_H
