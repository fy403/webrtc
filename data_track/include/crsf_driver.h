#ifndef CRSF_DRIVER_H
#define CRSF_DRIVER_H

#include "motor_controller_config.h"
#include "rc_protocol_v2.h"
#include <memory>
#include <cstdint>

class CRSFTransport;

/**
 * CRSFDriver - CRSF 协议驱动
 *
 * 直接驱动 CRSFTransport，将 ControlFrame 的 16 通道 raw PWM 直写到 CRSF 链路。
 */
class CRSFDriver {
public:
    explicit CRSFDriver(const MotorControllerConfig& config);
    ~CRSFDriver();

    void applyControl(const RCProtocolV2::ControlFrame& control_frame);
    void stopAll();
    void printStatus();

private:
    std::shared_ptr<CRSFTransport> transport_;
    uint16_t neutral_pwm_[16];            // 每个通道的中位值 (us)，来自配置
    uint16_t last_pwm_[16] = {};
};

#endif // CRSF_DRIVER_H
