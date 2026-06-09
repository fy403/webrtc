#include "crsf_driver.h"
#include "crsf_transport.h"
#include <iostream>

CRSFDriver::CRSFDriver(const MotorControllerConfig& config)
    : neutral_pwm_(config.crsf_neutral_pwm) {

    transport_ = std::make_shared<CRSFTransport>(config.motor_driver_port);
    if (!transport_->connect()) {
        throw std::runtime_error("Failed to connect CRSF transport");
    }
    std::cout << "CRSF Transport 初始化成功" << std::endl;

    for (int i = 0; i < 16; i++) {
        transport_->setChannelPwm(static_cast<uint8_t>(i), neutral_pwm_);
        last_pwm_[i] = neutral_pwm_;
    }
    std::cout << "CRSF 驱动初始化完成（16通道直通，中位=" << neutral_pwm_ << "us）" << std::endl;
}

CRSFDriver::~CRSFDriver() {}

void CRSFDriver::applyControl(const RCProtocolV2::ControlFrame& control_frame) {
    for (int i = 0; i < 16; i++) {
        last_pwm_[i] = static_cast<uint16_t>(control_frame.channels[i]);
        transport_->setChannelPwm(static_cast<uint8_t>(i), last_pwm_[i]);
    }
}

void CRSFDriver::stopAll() {
    for (int i = 0; i < 16; i++) {
        transport_->setChannelPwm(static_cast<uint8_t>(i), neutral_pwm_);
        last_pwm_[i] = neutral_pwm_;
    }
}

void CRSFDriver::printStatus() {
    std::cout << "CRSF: CH1=" << last_pwm_[0] << "us CH2=" << last_pwm_[1]
              << " CH3=" << last_pwm_[2] << " CH4=" << last_pwm_[3] << std::endl;
}
