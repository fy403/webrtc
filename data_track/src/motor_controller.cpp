#include "motor_controller.h"
#include "uart_motor_driver.h"
#include "crsf_motor_driver.h"
#include "crsf_transport.h"
#include "crsf_gimbal_driver.h"
#include "pwm_motor_driver.h"
#include "dummy_motor_driver.h"
#include "rc_protocol_v2.h"
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
        // === CRSF 模式：创建共享 Transport 层 ===
        // 1. 创建 CRSFTransport（独占串口 + 发送线程）
        crsf_transport_ = std::make_shared<CRSFTransport>(config.motor_driver_port);

        if (!crsf_transport_->connect()) {
            throw std::runtime_error("Failed to connect CRSF transport");
        }
        std::cout << "CRSF Transport 初始化成功" << std::endl;

        // 2. 创建电机驱动（共享 transport）
        motor_driver = new CRSFMotorDriver(crsf_transport_,
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

        if (!motor_driver->connect()) {
            throw std::runtime_error("Failed to connect CRSF motor driver");
        }
        std::cout << "CRSF 电机驱动初始化成功" << std::endl;

        // 3. 如果配置启用云台，创建云台驱动（共享同一个 transport）
        if (config.enable_gimbal) {
            gimbal_driver_ = std::make_shared<CRSFGimbalDriver>(crsf_transport_,
                                                                config.crsf_gimbal_tilt_min_pulse,
                                                                config.crsf_gimbal_tilt_max_pulse,
                                                                config.crsf_gimbal_tilt_neutral_pulse,
                                                                config.crsf_gimbal_tilt_min_angle,
                                                                config.crsf_gimbal_tilt_max_angle,
                                                                config.crsf_gimbal_tilt_channel,
                                                                config.crsf_gimbal_pan_min_pulse,
                                                                config.crsf_gimbal_pan_max_pulse,
                                                                config.crsf_gimbal_pan_neutral_pulse,
                                                                config.crsf_gimbal_pan_min_angle,
                                                                config.crsf_gimbal_pan_max_angle,
                                                                config.crsf_gimbal_pan_channel);

            gimbal_driver_->centerAll();
            std::cout << "CRSF 云台驱动初始化成功（通道"
                      << static_cast<int>(config.crsf_gimbal_tilt_channel) << "/"
                      << static_cast<int>(config.crsf_gimbal_pan_channel) << "）" << std::endl;
        } else {
            std::cout << "CRSF 云台控制未启用" << std::endl;
        }
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
        motor_driver = new PwmMotorDriver(config.pwm_front_back_chip,  // 前后控制PWM芯片
                                          config.pwm_left_right_chip,   // 左右转向PWM芯片
                                          config.pwm_period_ns,
                                          config.pwm_duty_min_ns,
                                          config.pwm_duty_max_ns,
                                          config.pwm_duty_neutral_ns,
                                          config.pwm_front_back_channel,
                                          config.pwm_left_right_channel);

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
    setFrontBackSpeed(0);
    setLeftRightSpeed(0);
    std::cout << "电机控制器初始化完成，设置为中位..." << std::endl;
}

MotorController::~MotorController() {
    stopAll();
    if (motor_driver) {
        motor_driver->disconnect();
        delete motor_driver;
    }
    // CRSFTransport 通过 shared_ptr 自动析构（停止发送线程）
    // CRSFGimbalDriver 通过 shared_ptr 自动析构
}


void MotorController::stopAll() {
    setFrontBackSpeed(0);
    setLeftRightSpeed(0);
    if (gimbal_driver_) {
        gimbal_driver_->centerAll();
    }
    //    std::cout << "已停止所有电机" << std::endl;
}

void MotorController::printStatus() {
    std::cout << "前进电机: " << front_back_speed_ << "% | 转向电机: " << left_right_speed_ << "%";
    if (config_.enable_gimbal) {
        std::cout << " | 云台俯仰: " << tilt_percent_ << "% | 云台水平: " << pan_percent_ << "%";
    }
    std::cout << std::endl;
}

void MotorController::emergencyStop() {
    stopAll();
    std::cout << "紧急停止执行" << std::endl;
}

void MotorController::applyControl(const RCProtocolV2::ControlFrame &control_frame) {
    constexpr double DEADZONE = 0.02;
    auto clamp_unit = [](double v) { return std::max(-1.0, std::min(1.0, v)); };
    const auto to_percent = [](double v) { return static_cast<int>(std::round(v * 100.0)); };

    // ========== 电机控制：根据配置通道读取 ControlFrame ==========
    int esc_ch = config_.crsf_esc_channel - 1;     // 电调通道（前后）
    int servo_ch = config_.crsf_servo_channel - 1;  // 舵机通道（左右）

    double forward = control_frame.channels[esc_ch];
    double turn = control_frame.channels[servo_ch];

    forward = clamp_unit(forward);
    turn = clamp_unit(turn);

    if (std::fabs(forward) < DEADZONE) forward = 0.0;
    if (std::fabs(turn) < DEADZONE) turn = 0.0;

    // 后退时根据配置反转转向方向
    if (forward < 0 && config_.reverse_turn_when_backward) {
        turn = -turn;
    }

    setFrontBackSpeed(to_percent(forward));
    setLeftRightSpeed(to_percent(turn));

    // ========== 云台控制：根据配置通道读取 ControlFrame ==========
    if (gimbal_driver_ && config_.enable_gimbal) {
        int tilt_ch = config_.crsf_gimbal_tilt_channel - 1;
        int pan_ch = config_.crsf_gimbal_pan_channel - 1;

        double tilt = control_frame.channels[tilt_ch];
        double pan = control_frame.channels[pan_ch];

        tilt = clamp_unit(tilt);
        pan = clamp_unit(pan);

        if (std::fabs(tilt) < DEADZONE) tilt = 0.0;
        if (std::fabs(pan) < DEADZONE) pan = 0.0;

        tilt_percent_ = to_percent(tilt);
        pan_percent_ = to_percent(pan);

        gimbal_driver_->setTiltPercent(tilt_percent_);
        gimbal_driver_->setPanPercent(pan_percent_);
    }

    printStatus();
}

void MotorController::setFrontBackSpeed(int speed_percent) {
    front_back_speed_ = speed_percent;
    if (motor_driver) {
        motor_driver->setFrontBackPercent(speed_percent);
    }
    const char *direction = (speed_percent > 0) ? "前进" : (speed_percent < 0 ? "后退" : "停止");
    // std::cout << "控制前后电机速度: " << speed_percent << "% (direction: " << direction << ")" << std::endl;
}

void MotorController::setLeftRightSpeed(int speed_percent) {
    left_right_speed_ = speed_percent;
    if (motor_driver) {
        motor_driver->setLeftRightPercent(speed_percent);
    }
    const char *direction = (speed_percent > 0) ? "右转" : (speed_percent < 0 ? "左转" : "停止");
    // std::cout << "控制转向电机速度: " << speed_percent << "% (direction: " << direction << ")" << std::endl;
}
