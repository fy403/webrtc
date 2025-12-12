#include "motor_controller.h"
#include "uart_motor_driver.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

MotorController::MotorController(const MotorControllerConfig &config) {
    // 初始化电机速度
    for (int i = 0; i < 4; i++) {
        motor_speeds[i] = 0;
    }

    // 根据配置中的 motor_driver_type 创建相应的驱动实例
    if (config.motor_driver_type == "uart") {
        motor_driver = new UartMotorDriver(config.motor_driver_port, config.motor_driver_baudrate);
    } else {
        std::cerr << "Unknown motor driver type: " << config.motor_driver_type << std::endl;
        throw std::runtime_error("Unknown motor driver type");
    }

    // 尝试初始化串口驱动
    // try
    {
        if (!motor_driver->connect()) {
            throw std::runtime_error("Failed to connect to serial port");
        }

        // 简单测试
        std::string response;

        std::cout << "=== Testing motor type setting ===" << std::endl;
        if (motor_driver->setMotorType(4, &response)) {
            std::cout << "Motor type set successfully. Response: " << response << std::endl;
        } else {
            std::cout << "Failed to set motor type." << std::endl;
            throw std::runtime_error("Failed to set motor type.");
        }
        if (motor_driver->readBatteryVoltage(&response)) {
            std::cout << "串口驱动初始化成功，电池电压: " << response << std::endl;
        } else {
            std::cout << "串口驱动初始化成功，但无法读取电池电压" << std::endl;
            throw std::runtime_error("Faild to read battery params");
        }
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

MotorController::~MotorController() {
    stopAll();
    if (motor_driver) {
        motor_driver->disconnect();
        delete motor_driver;
    }
}

void MotorController::setNeutral() {
    if (motor_driver) {
        motor_driver->setPWM(NEUTRAL_PWM, NEUTRAL_PWM, NEUTRAL_PWM, NEUTRAL_PWM);
    } else {
        std::cout << "模拟模式中位: PWM=0" << std::endl;
    }

    for (int i = 0; i < 4; i++) {
        motor_speeds[i] = 0;
    }
}

int16_t MotorController::speedToPWM(int speed_percent) {
    if (speed_percent > 0) {
        return static_cast<int16_t>((speed_percent / 100.0) * MAX_FORWARD_PWM);
    } else if (speed_percent < 0) {
        return static_cast<int16_t>((speed_percent / 100.0) * -MAX_REVERSE_PWM);
    } else {
        return NEUTRAL_PWM;
    }
}

void MotorController::setMotorSpeed(int motor_id, int speed_percent) {
    if (motor_id < 1 || motor_id > 4)
        return;

    int16_t pwm_value = speedToPWM(speed_percent);

    // 根据说明，M2控制前进后退，M4控制左右转向
    // 只更新目标电机，不要清零其他电机，以避免后续调用覆盖前一个轴的指令
    if (motor_driver) {
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

void MotorController::stopAll() {
    setNeutral();
    std::cout << "已停止所有电机" << std::endl;
}

void MotorController::printStatus() {
    std::cout << "前进电机: " << motor_speeds[0] << "% | ";
    std::cout << "转向电机: " << motor_speeds[1] << "%" << std::endl;
}

void MotorController::emergencyStop() {
    stopAll();
    std::cout << "紧急停止执行" << std::endl;
}

void MotorController::applySbus(double forward, double turn) {
    constexpr double DEADZONE = 0.02;
    auto clamp_unit = [](double v) { return std::max(-1.0, std::min(1.0, v)); };

    forward = clamp_unit(forward);
    turn = clamp_unit(turn);

    if (std::fabs(forward) < DEADZONE) {
        forward = 0.0;
    }
    if (std::fabs(turn) < DEADZONE) {
        turn = 0.0;
    }

    const auto to_percent = [](double v) { return static_cast<int>(std::round(v * 100.0)); };

    const int forward_percent = to_percent(forward);
    const int turn_percent = to_percent(turn);

    setMotorSpeed(MOTOR_FRONT_BACK, forward_percent);
    setMotorSpeed(MOTOR_LEFT_RIGHT, turn_percent);
    printStatus();
}
