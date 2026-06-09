#include "motor_controller.h"
#include "pwm_motor_driver.h"
#include "dummy_motor_driver.h"
#include "crsf_driver.h"
#include "rc_protocol_v2.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstring>

MotorController::MotorController(const MotorControllerConfig &config)
    : config_(config),
      motor_driver(nullptr) {
    // 根据配置中的 motor_driver_type 创建相应的驱动实例
    if (config.motor_driver_type == "crsf") {
        crsf_driver_ = std::make_unique<CRSFDriver>(config);
        motor_driver = nullptr;
    } else if (config.motor_driver_type == "dummy") {
        // 创建虚拟电机驱动器（用于调试，只打印信号不执行）
        motor_driver = new DummyMotorDriver("DummyMotor-" + config.motor_driver_port);
        
        if (!motor_driver->connect()) {
            throw std::runtime_error("Failed to connect dummy motor driver");
        }
        std::cout << "Dummy 驱动初始化成功（调试模式，只打印信号）" << std::endl;
    } else if (config.motor_driver_type == "pwm") {
        // 创建 PWM 电机驱动器
        // PWM8_M0 (Pin 15) 用于前后控制
        // PWM9_M0 (Pin 18) 用于左右转向
        motor_driver = new PwmMotorDriver(config.pwm_front_back_chip,
                                          config.pwm_left_right_chip,
                                          config.pwm_period_ns,
                                          config.pwm_front_back_channel,
                                          config.pwm_left_right_channel,
                                          config.pwm_proto_forward_idx,
                                          config.pwm_proto_turn_idx,
                                          config.pwm_neutral_pwm);

        // 尝试初始化 PWM 驱动
        if (!motor_driver->connect()) {
            throw std::runtime_error("Failed to connect to PWM motor driver");
        }
        std::cout << "PWM 驱动初始化成功" << std::endl;
        std::cout << "  前后控制: pwmchip" << config.pwm_front_back_chip 
                  << "/pwm" << config.pwm_front_back_channel << std::endl;
        std::cout << "  左右转向: pwmchip" << config.pwm_left_right_chip 
                  << "/pwm" << config.pwm_left_right_channel << std::endl;
    } else {
        std::cerr << "Unknown motor driver type: " << config.motor_driver_type << std::endl;
        throw std::runtime_error("Unknown motor driver type");
    }

    // 设置电机中位
    if (motor_driver) {
        motor_driver->stopAll();
    }
    std::cout << "电机控制器初始化完成，设置为中位..." << std::endl;
}

MotorController::~MotorController() {
    stopAll();
    if (motor_driver) {
        motor_driver->disconnect();
        delete motor_driver;
    }
    // crsf_driver_ 通过 unique_ptr 自动析构
}


void MotorController::stopAll() {
    if (crsf_driver_) {
        crsf_driver_->stopAll();
    } else if (motor_driver) {
        motor_driver->stopAll();
    }
}

void MotorController::printStatus() {
    if (crsf_driver_) {
        crsf_driver_->printStatus();
    }
}

void MotorController::emergencyStop() {
    stopAll();
    std::cout << "紧急停止执行" << std::endl;
}

void MotorController::applyControl(const RCProtocolV2::ControlFrame &control_frame) {
    if (crsf_driver_) {
        crsf_driver_->applyControl(control_frame);
    } else if (motor_driver) {
        motor_driver->applyControl(control_frame);
    }

    printStatus();
}
