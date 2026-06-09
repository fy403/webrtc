#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

// 前置声明，避免基类头文件依赖协议细节
namespace RCProtocolV2 { struct ControlFrame; }

class MotorDriver {
public:
    MotorDriver() = default;

    virtual ~MotorDriver() = default;

    virtual bool connect() = 0;

    virtual void disconnect() = 0;

    // 接收原始控制帧，由各驱动自行决定如何解析通道和映射
    // （CRSF 驱动通过 CRSFController 单独处理，不使用此接口）
    virtual void applyControl(const RCProtocolV2::ControlFrame& frame) = 0;

    // 停止所有电机，默认空实现（CRSF 路径由 CRSFController::stopAll() 接管）
    virtual void stopAll() {}
};

#endif // MOTOR_DRIVER_H