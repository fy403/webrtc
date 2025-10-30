#include "motor_controller.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

MotorControllerTTY::MotorControllerTTY(const std::string &port)
{
    // 初始化电机速度
    for (int i = 0; i < 4; i++)
    {
        motor_speeds[i] = 0;
    }
    for (int i = 0; i < 4; i++)
    {
        key_states[i] = false;
    }

    // 尝试初始化串口驱动
    // try
    {
        motor_driver = new MotorDriver(port, 115200);
        if (!motor_driver->connect())
        {
            throw std::runtime_error("Failed to connect to serial port");
        }

        // 简单测试
        std::string response;

        std::cout << "=== Testing motor type setting ===" << std::endl;
        if (motor_driver->setMotorType(4, &response))
        {
            std::cout << "Motor type set successfully. Response: " << response << std::endl;
        }
        else
        {
            std::cout << "Failed to set motor type." << std::endl;
            throw std::runtime_error("Failed to set motor type.");
        }
        if (motor_driver->readBatteryVoltage(&response))
        {
            std::cout << "串口驱动初始化成功，电池电压: " << response << std::endl;
        }
        else
        {
            std::cout << "串口驱动初始化成功，但无法读取电池电压" << std::endl;
            throw std::runtime_error("Faild to read battery params");
        }
        simulation_mode = false;
    }
    // catch (const std::exception &e)
    //  {
    //      std::cerr << "错误: 串口初始化失败，使用模拟模式: " << e.what() << std::endl;
    //      motor_driver = nullptr;
    //      simulation_mode = true;
    //  }

    // 设置电机中位
    setNeutral();
    // std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "电机控制器初始化完成，设置为中位..." << std::endl;
}

MotorControllerTTY::~MotorControllerTTY()
{
    stopAll();
    if (motor_driver)
    {
        motor_driver->disconnect();
        delete motor_driver;
    }
}

void MotorControllerTTY::setNeutral()
{
    if (!simulation_mode && motor_driver)
    {
        motor_driver->setPWM(NEUTRAL_PWM, NEUTRAL_PWM, NEUTRAL_PWM, NEUTRAL_PWM);
    }
    else
    {
        std::cout << "模拟模式中位: PWM=0" << std::endl;
    }

    for (int i = 0; i < 4; i++)
    {
        motor_speeds[i] = 0;
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int16_t MotorControllerTTY::speedToPWM(int speed_percent)
{
    if (speed_percent > 0)
    {
        return static_cast<int16_t>((speed_percent / 100.0) * MAX_FORWARD_PWM);
    }
    else if (speed_percent < 0)
    {
        return static_cast<int16_t>((speed_percent / 100.0) * -MAX_REVERSE_PWM);
    }
    else
    {
        return NEUTRAL_PWM;
    }
}

void MotorControllerTTY::setMotorSpeed(int motor_id, int speed_percent)
{
    if (motor_id < 1 || motor_id > 4)
        return;

    int16_t pwm_value = speedToPWM(speed_percent);

    // 根据说明，M2控制前进后退，M4控制左右转向
    if (!simulation_mode && motor_driver)
    {
        motor_speeds[0], motor_speeds[1], motor_speeds[2], motor_speeds[3] = NEUTRAL_PWM, NEUTRAL_PWM, NEUTRAL_PWM, NEUTRAL_PWM;
        motor_speeds[motor_id - 1] = pwm_value;
        motor_driver->setPWM(motor_speeds[0], motor_speeds[1], motor_speeds[2], motor_speeds[3]);
    }

    const char *direction = "停止";
    if (speed_percent > 0)
        direction = "正转";
    else if (speed_percent < 0)
        direction = "反转";

    const char *motor_function = (motor_id == MOTOR_FRONT_BACK) ? "前进" : "转向";
    std::cout << "控制" << motor_function << "电机" << motor_id << "速度: " << speed_percent
              << "% (PWM: " << pwm_value << ")" << std::endl;
}

void MotorControllerTTY::cycleThrottle()
{
    throttle_level = (throttle_level + 1) % THROTTLE_LEVEL_COUNT;
    std::cout << "油门档位: " << THROTTLE_LEVELS[throttle_level].level
              << " (" << THROTTLE_LEVELS[throttle_level].speed_percent << "%)" << std::endl;
    updateMotors();
}

void MotorControllerTTY::updateMotors()
{
    int base_speed = THROTTLE_LEVELS[throttle_level].speed_percent;
    int speed_front_back = 0;
    int speed_left_right = 0;

    bool w = key_states[0];
    bool s = key_states[1];
    bool a = key_states[2];
    bool d = key_states[3];

    // 优先判断方向组合
    if (w && !s)
    { // 前进
        speed_front_back = base_speed;

        if (a && !d)
            speed_left_right = -base_speed; // 前进+左转
        else if (d && !a)
            speed_left_right = base_speed; // 前进+右转
    }
    else if (s && !w)
    { // 后退
        speed_front_back = -base_speed;

        if (a && !d)
            speed_left_right = base_speed; // 后退+左转
        else if (d && !a)
            speed_left_right = -base_speed; // 后退+右转
    }

    // 只有在没有方向键按下时才允许原地转向
    else if (a && !d && !w && !s)
    { // 原地左转
        speed_left_right = -base_speed;
    }
    else if (d && !a && !w && !s)
    { // 原地右转
        speed_left_right = base_speed;
    }

    // 如果同时按下相反方向键，停止相应电机
    if ((w && s) || (a && d))
    {
        // 冲突按键，停止所有
        speed_front_back = 0;
        speed_left_right = 0;
    }

    setMotorSpeed(MOTOR_FRONT_BACK, speed_front_back);
    setMotorSpeed(MOTOR_LEFT_RIGHT, speed_left_right);
    printStatus();
}

void MotorControllerTTY::printStatus()
{
    std::cout << "档位: " << THROTTLE_LEVELS[throttle_level].level << "档 | ";
    std::cout << "前进电机: " << motor_speeds[0] << "% | ";
    std::cout << "转向电机: " << motor_speeds[1] << "%" << std::endl;
}

bool MotorControllerTTY::setKeyState(const char *key, bool state)
{
    for (int i = 0; i < 4; i++)
    {
        if (strcmp(key, KEY_NAMES[i]) == 0)
        {
            if (key_states[i] != state)
            {
                key_states[i] = state;
                updateMotors();
                return true;
            }
            return false;
        }
    }
    return false;
}

void MotorControllerTTY::stopAll()
{
    setNeutral();
    for (int i = 0; i < 4; i++)
    {
        key_states[i] = false;
    }
    std::cout << "已停止所有电机" << std::endl;
}

void MotorControllerTTY::emergencyStop()
{
    throttle_level = 0;
    stopAll();
    std::cout << "紧急停止执行" << std::endl;
}
