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

    // 实现 MotorDriver 接口
    bool connect() override;
    
    void disconnect() override;
    
    void setMotorPercent(int motor_id, int percent) override;
    
    void setFrontBackPercent(int percent) override;
    
    void setLeftRightPercent(int percent) override;

private:
    std::string name_;
    bool connected_;
    
    // 打印辅助函数
    void printMotorCommand(const std::string &command, int motor_id, int percent);
    void printControlCommand(const std::string &direction, int percent);
};

#endif // DUMMY_MOTOR_DRIVER_H
