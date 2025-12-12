#ifndef UART_MOTOR_DRIVER_H
#define UART_MOTOR_DRIVER_H

#include "motor_driver.h"
#include <string>

class UartMotorDriver : public MotorDriver
{
public:
    UartMotorDriver(const std::string &port, int baudrate = 115200);
    ~UartMotorDriver();

    // 实现纯虚函数
    bool connect() override;
    void disconnect() override;
    bool setMotorType(int type, std::string *response = nullptr) override;
    bool readBatteryVoltage(std::string *response = nullptr) override;
    void setPWM(int m1, int m2, int m3, int m4) override;

    // 实现未使用的函数
    bool setDeadZone(int deadzone, std::string *response = nullptr) override;
    bool setMotorLine(int lines, std::string *response = nullptr) override;
    bool setMotorPhase(int phase, std::string *response = nullptr) override;
    bool setWheelDiameter(int diameter, std::string *response = nullptr) override;
    bool setPID(float p, float i, float d, std::string *response = nullptr) override;
    bool resetToFactory(std::string *response = nullptr) override;
    bool setEncoderUpload(bool total, bool realtime, bool speed, std::string *response = nullptr) override;
    bool readFlash(std::string *response = nullptr) override;

private:
    std::string port_;
    int baudrate_;
    int fd_; // 串口文件描述符
    int error_count_; // 错误计数器

    bool sendCommand(const std::string &cmd, std::string *response, int timeout_ms = 500);
    bool openSerial();
    void closeSerial();
    
    void checkErrorCount(); // 检查错误计数并根据需要退出程序
};

#endif // UART_MOTOR_DRIVER_H

