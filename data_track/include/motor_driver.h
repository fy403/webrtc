#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <string>
#include <vector>
#include <iostream>

class MotorDriver
{
public:
    MotorDriver() = default;
    virtual ~MotorDriver() = default;

    // 已使用的函数 - 纯虚函数，必须由子类实现
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool setMotorType(int type, std::string *response = nullptr) = 0;
    virtual bool readBatteryVoltage(std::string *response = nullptr) = 0;
    virtual void setPWM(int m1, int m2, int m3, int m4) = 0;

    // 未使用的函数 - 虚函数，提供默认实现（提示未实现）
    virtual bool setDeadZone(int deadzone, std::string *response = nullptr) {
        std::cerr << "Warning: setDeadZone() is not implemented for this motor driver type" << std::endl;
        if (response) *response = "Not implemented";
        return false;
    }

    virtual bool setMotorLine(int lines, std::string *response = nullptr) {
        std::cerr << "Warning: setMotorLine() is not implemented for this motor driver type" << std::endl;
        if (response) *response = "Not implemented";
        return false;
    }

    virtual bool setMotorPhase(int phase, std::string *response = nullptr) {
        std::cerr << "Warning: setMotorPhase() is not implemented for this motor driver type" << std::endl;
        if (response) *response = "Not implemented";
        return false;
    }

    virtual bool setWheelDiameter(int diameter, std::string *response = nullptr) {
        std::cerr << "Warning: setWheelDiameter() is not implemented for this motor driver type" << std::endl;
        if (response) *response = "Not implemented";
        return false;
    }

    virtual bool setPID(float p, float i, float d, std::string *response = nullptr) {
        std::cerr << "Warning: setPID() is not implemented for this motor driver type" << std::endl;
        if (response) *response = "Not implemented";
        return false;
    }

    virtual bool resetToFactory(std::string *response = nullptr) {
        std::cerr << "Warning: resetToFactory() is not implemented for this motor driver type" << std::endl;
        if (response) *response = "Not implemented";
        return false;
    }

    virtual bool setEncoderUpload(bool total, bool realtime, bool speed, std::string *response = nullptr) {
        std::cerr << "Warning: setEncoderUpload() is not implemented for this motor driver type" << std::endl;
        if (response) *response = "Not implemented";
        return false;
    }

    virtual bool readFlash(std::string *response = nullptr) {
        std::cerr << "Warning: readFlash() is not implemented for this motor driver type" << std::endl;
        if (response) *response = "Not implemented";
        return false;
    }
};

#endif // MOTOR_DRIVER_H