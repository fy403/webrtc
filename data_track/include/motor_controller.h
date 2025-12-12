#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include "motor_driver.h"
#include "motor_controller_config.h"
#include "constants.h"

class MotorController {
public:
    /**
     * 构造函数 - 使用配置类
     * @param config MotorController 配置，包含 MotorDriver 的配置参数
     */
    explicit MotorController(const MotorControllerConfig &config = MotorControllerConfig());

    ~MotorController();

    void setNeutral();

    void stopAll();

    void printStatus();

    void emergencyStop();

    void applySbus(double forward, double turn);

private:
    MotorDriver *motor_driver;

    int16_t speedToPWM(int speed_percent);

    std::atomic<int16_t> motor_speeds[4];

    void setMotorSpeed(int motor_id, int speed_percent);
};

#endif // MOTOR_CONTROLLER_H