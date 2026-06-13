#include "crsf_driver.h"
#include "crsf_transport.h"
#include <iostream>

CRSFDriver::CRSFDriver(const MotorControllerConfig& config) {
    // 从配置加载每个通道的中位值
    for (int i = 0; i < 16; i++) {
        neutral_pwm_[i] = config.crsf_neutral_pwm[i];
    }

    transport_ = std::make_shared<CRSFTransport>(config.motor_driver_port);
    if (!transport_->connect()) {
        throw std::runtime_error("Failed to connect CRSF transport");
    }
    std::cout << "CRSF Transport 初始化成功" << std::endl;

    for (int i = 0; i < 16; i++) {
        transport_->setChannelPWM(static_cast<uint8_t>(i), neutral_pwm_[i]);
        last_pwm_[i] = neutral_pwm_[i];
    }
    std::cout << "CRSF 驱动初始化完成（16通道直通）" << std::endl;
}

CRSFDriver::~CRSFDriver() {}

void CRSFDriver::applyControl(const RCProtocolV2::ControlFrame& control_frame) {
    for (int i = 0; i < 16; i++) {
        last_pwm_[i] = static_cast<uint16_t>(control_frame.channels[i]);
        transport_->setChannelPWM(static_cast<uint8_t>(i), last_pwm_[i]);
    }
}

void CRSFDriver::stopAll() {
    for (int i = 0; i < 16; i++) {
        transport_->setChannelPWM(static_cast<uint8_t>(i), neutral_pwm_[i]);
        last_pwm_[i] = neutral_pwm_[i];
    }
}

void CRSFDriver::printStatus() {
    // 收集非中位的通道
    bool has_active = false;
    std::cout << "CRSF: ";
    for (int i = 0; i < 16; i++) {
        if (last_pwm_[i] != neutral_pwm_[i]) {
            if (has_active) std::cout << " ";
            std::cout << "CH" << (i + 1) << "=" << last_pwm_[i] << "us";
            has_active = true;
        }
    }
    // if (!has_active) std::cout << "待机中";
    std::cout << std::endl;
}
