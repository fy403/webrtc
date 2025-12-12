#ifndef UART_MOTOR_DRIVER_H
#define UART_MOTOR_DRIVER_H

#include "motor_driver.h"
#include <string>

class UartMotorDriver : public MotorDriver {
public:
    UartMotorDriver(const std::string &port,
                    int baudrate = 115200,
                    int16_t forward_max = 3500,
                    int16_t reverse_max = -3500,
                    int16_t neutral = 0,
                    int front_back_id = 2,
                    int left_right_id = 4);

    ~UartMotorDriver();

    // 实现纯虚函数
    bool connect() override;

    void disconnect() override;

    void setMotorPercent(int motor_id, int percent) override;

    void setFrontBackPercent(int percent) override;

    void setLeftRightPercent(int percent) override;

private:
    std::string port_;
    int baudrate_;
    int fd_; // 串口文件描述符
    int error_count_; // 错误计数器
    int16_t forward_limit_;
    int16_t reverse_limit_;
    int16_t neutral_pwm_;
    int16_t motor_percents_[4]{0, 0, 0, 0};
    int front_back_id_;
    int left_right_id_;

    int16_t percentToPwm(int speed_percent) const;

    bool sendCommand(const std::string &cmd, std::string *response, int timeout_ms = 500);

    bool openSerial();

    void closeSerial();

    void checkErrorCount(); // 检查错误计数并根据需要退出程序


    bool readBatteryVoltage(std::string *response = nullptr);

    bool setMotorType(int type, std::string *response = nullptr);

    void setPWMPercent(int m1_percent, int m2_percent, int m3_percent, int m4_percent);

    // 实现未使用的函数
    bool setDeadZone(int deadzone, std::string *response = nullptr);

    bool setMotorLine(int lines, std::string *response = nullptr);

    bool setMotorPhase(int phase, std::string *response = nullptr);

    bool setWheelDiameter(int diameter, std::string *response = nullptr);

    bool setPID(float p, float i, float d, std::string *response = nullptr);

    bool resetToFactory(std::string *response = nullptr);

    bool setEncoderUpload(bool total, bool realtime, bool speed, std::string *response = nullptr);

    bool readFlash(std::string *response = nullptr);
};

#endif // UART_MOTOR_DRIVER_H

