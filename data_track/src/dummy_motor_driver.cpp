#include "dummy_motor_driver.h"
#include <iostream>
#include <chrono>
#include <iomanip>

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

void DummyMotorDriver::setMotorPercent(int motor_id, int percent) {
    printMotorCommand("setMotorPercent", motor_id, percent);
}

void DummyMotorDriver::setFrontBackPercent(int percent) {
    printControlCommand("FrontBack", percent);
}

void DummyMotorDriver::setLeftRightPercent(int percent) {
    printControlCommand("LeftRight", percent);
}

void DummyMotorDriver::printMotorCommand(const std::string &command, int motor_id, int percent) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::cout << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "] "
              << "[DummyMotor] " << command 
              << " - Motor ID: " << motor_id 
              << ", Percent: " << percent 
              << "%, Connected: " << (connected_ ? "Yes" : "No") 
              << std::endl;
}

void DummyMotorDriver::printControlCommand(const std::string &direction, int percent) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::cout << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "] "
              << "[DummyMotor] " << direction 
              << " control - Percent: " << percent 
              << "%, Connected: " << (connected_ ? "Yes" : "No") 
              << std::endl;
}
