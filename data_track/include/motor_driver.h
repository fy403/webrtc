#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

class MotorDriver {
public:
    MotorDriver() = default;

    virtual ~MotorDriver() = default;

    virtual bool connect() = 0;

    virtual void disconnect() = 0;

    // 传入单路百分比（-100~100），由驱动内部维护/组合并下发
    virtual void setMotorPercent(int motor_id, int percent) = 0;

    // 面向前后/左右的语义化接口，由驱动内部持有 motor_id
    virtual void setFrontBackPercent(int percent) = 0;

    virtual void setLeftRightPercent(int percent) = 0;
};

#endif // MOTOR_DRIVER_H