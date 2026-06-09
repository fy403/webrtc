#include "dummy_motor_driver.h"
#include "rc_protocol_v2.h"
#include <iostream>

DummyMotorDriver::DummyMotorDriver(const std::string &name)
    : name_(name), connected_(false) {
    std::cout << "[DummyMotor] Created: " << name_ << std::endl;
}

DummyMotorDriver::~DummyMotorDriver() {
    disconnect();
    std::cout << "[DummyMotor] Destroyed: " << name_ << std::endl;
}

bool DummyMotorDriver::connect() {
    std::cout << "[DummyMotor] Connecting..." << std::endl;
    connected_ = true;
    std::cout << "[DummyMotor] Connected successfully (dummy mode)" << std::endl;
    return true;
}

void DummyMotorDriver::disconnect() {
    if (connected_) {
        std::cout << "[DummyMotor] Disconnecting..." << std::endl;
        connected_ = false;
        std::cout << "[DummyMotor] Disconnected (dummy mode)" << std::endl;
    }
}

void DummyMotorDriver::applyControl(const RCProtocolV2::ControlFrame &frame) {
    for (int i = 0; i < 16; i++) {
        std::cout << "CH" << (i + 1) << "=" << static_cast<uint16_t>(frame.channels[i]) << " ";
    }
    std::cout << std::endl;
}

void DummyMotorDriver::stopAll() {
    std::cout << "[DummyMotor] stopAll" << std::endl;
}
