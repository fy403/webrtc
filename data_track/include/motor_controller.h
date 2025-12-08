#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include "motor_driver.h"
#include "constants.h"

class MotorControllerTTY
{
public:
    MotorControllerTTY(const std::string &port = "/dev/ttyUSB0");
    ~MotorControllerTTY();

    void setNeutral();
    void stopAll();
    void printStatus();
    void emergencyStop();
    void applySbus(double forward, double turn);

private:
    MotorDriver *motor_driver;
    int16_t speedToPWM(int speed_percent);
    std::atomic<int16_t> motor_speeds[4]; // 只记录2个电机的速度
    void setMotorSpeed(int motor_id, int speed_percent);
};

#endif // MOTOR_CONTROLLER_H