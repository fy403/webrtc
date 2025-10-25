#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include "motor_driver.h"
#include "constants.h"
#include "throttle_levels.h"

class MotorControllerTTY
{
public:
    // In motor_controller.cpp, line 7 should be:
    MotorControllerTTY(const std::string &port);
    ~MotorControllerTTY();

    void setNeutral();
    void cycleThrottle();
    void updateMotors();
    void printStatus();
    bool setKeyState(const char *key, bool state);
    void stopAll();
    void emergencyStop();

private:
    MotorDriver *motor_driver;
    std::atomic<bool> simulation_mode = false;

    // 电机状态
    std::atomic<int> throttle_level = 0;
    std::atomic<int16_t> motor_speeds[4]; // 只记录2个电机的速度
    std::atomic<bool> key_states[4];      // w,s,a,d

    // 按键映射
    const char *KEY_NAMES[4] = {"w", "s", "a", "d"};

    int16_t speedToPWM(int speed_percent);
    void setMotorSpeed(int motor_id, int speed_percent);
};

#endif // MOTOR_CONTROLLER_H