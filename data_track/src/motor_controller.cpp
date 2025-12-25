#include "motor_controller.h"
#include "uart_motor_driver.h"
#include "crsf_motor_driver.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

MotorController::MotorController(const MotorControllerConfig &config)
        : config_(config),
          motor_driver(nullptr),
          front_back_speed_(0),
          left_right_speed_(0) {

    // 根据配置中的 motor_driver_type 创建相应的驱动实例
    if (config.motor_driver_type == "uart") {
        motor_driver = new UartMotorDriver(config.motor_driver_port,
                                           config.motor_driver_baudrate,
                                           config.motor_pwm_forward_max,
                                           config.motor_pwm_reverse_max,
                                           config.motor_pwm_neutral,
                                           config.motor_front_back_id,
                                           config.motor_left_right_id);

        // 尝试初始化串口驱动
        if (!motor_driver->connect()) {
            throw std::runtime_error("Failed to connect to serial port");
        }
    } else if (config.motor_driver_type == "crsf") {
        motor_driver = new CRSFMotorDriver(config.motor_driver_port,
                                           config.crsf_servo_min_pulse,
                                           config.crsf_servo_max_pulse,
                                           config.crsf_servo_neutral_pulse,
                                           config.crsf_servo_min_angle,
                                           config.crsf_servo_max_angle,
                                           config.crsf_servo_channel,
                                           config.crsf_esc_min_pulse,
                                           config.crsf_esc_max_pulse,
                                           config.crsf_esc_neutral_pulse,
                                           config.crsf_esc_reversible,
                                           config.crsf_esc_channel);

        // 尝试初始化 CRSF 驱动
        if (!motor_driver->connect()) {
            throw std::runtime_error("Failed to connect to CRSF serial port");
        }
        std::cout << "CRSF 驱动初始化成功" << std::endl;
    } else {
        std::cerr << "Unknown motor driver type: " << config.motor_driver_type << std::endl;
        throw std::runtime_error("Unknown motor driver type");
    }
    // catch (const std::exception &e)
    //  {
    //      std::cerr << "错误: 串口初始化失败，使用模拟模式: " << e.what() << std::endl;
    //      motor_driver = nullptr;
    //      simulation_mode = true;
    //  }

    // 设置电机中位
    setFrontBackSpeed(0);
    setLeftRightSpeed(0);
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


void MotorController::stopAll() {
    setFrontBackSpeed(0);
    setLeftRightSpeed(0);
//    std::cout << "已停止所有电机" << std::endl;
}

void MotorController::printStatus() {
    std::cout << "前进电机: " << front_back_speed_ << "% | ";
    std::cout << "转向电机: " << left_right_speed_ << "%" << std::endl;
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

    // 当后退时，根据配置决定是否反转转向方向（因为车辆倒车时转向方向需要反转）
    if (forward < 0 && config_.reverse_turn_when_backward) {
        turn = -turn;
    }


    const auto to_percent = [](double v) { return static_cast<int>(std::round(v * 100.0)); };

    const int forward_percent = to_percent(forward);
    const int turn_percent = to_percent(turn);

    setFrontBackSpeed(forward_percent);
    setLeftRightSpeed(turn_percent);
    printStatus();
}

void MotorController::setFrontBackSpeed(int speed_percent) {
    front_back_speed_ = speed_percent;
    if (motor_driver) {
        motor_driver->setFrontBackPercent(speed_percent);
    }
    const char *direction = (speed_percent > 0) ? "前进" : (speed_percent < 0 ? "后退" : "停止");
//    std::cout << "控制前后电机速度: " << speed_percent << "% (direction: " << direction << ")" << std::endl;
}

void MotorController::setLeftRightSpeed(int speed_percent) {
    left_right_speed_ = speed_percent;
    if (motor_driver) {
        motor_driver->setLeftRightPercent(speed_percent);
    }
    const char *direction = (speed_percent > 0) ? "右转" : (speed_percent < 0 ? "左转" : "停止");
//    std::cout << "控制转向电机速度: " << speed_percent << "% (direction: " << direction << ")" << std::endl;
}
