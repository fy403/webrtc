#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <string>
#include <memory>
#include "motor_driver.h"
#include "motor_controller_config.h"
#include "constants.h"
#include "rc_protocol_v2.h"

class CRSFDriver;

class MotorController {
public:
    explicit MotorController(const MotorControllerConfig &config = MotorControllerConfig());
    ~MotorController();

    void stopAll();
    void printStatus();
    void emergencyStop();

    void applyControl(const RCProtocolV2::ControlFrame &control_frame);

private:
    MotorControllerConfig config_;
    MotorDriver *motor_driver;
    std::unique_ptr<CRSFDriver> crsf_driver_;
};

#endif // MOTOR_CONTROLLER_H