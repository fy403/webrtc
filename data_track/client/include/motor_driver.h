#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <string>
#include <vector>

class MotorDriver
{
public:
    MotorDriver(const std::string &port, int baudrate = 115200);
    ~MotorDriver();

    bool connect();
    void disconnect();

    // 1. 配置电机类型
    bool setMotorType(int type, std::string *response = nullptr);

    // 2. 配置电机死区
    bool setDeadZone(int deadzone, std::string *response = nullptr);

    // 3. 配置电机相位线
    bool setMotorLine(int lines, std::string *response = nullptr);

    // 4. 配置电机减速比
    bool setMotorPhase(int phase, std::string *response = nullptr);

    // 5. 配置轮子直径（可选）
    bool setWheelDiameter(int diameter, std::string *response = nullptr);

    // 6. 配置电机控制的PID参数
    bool setPID(float p, float i, float d, std::string *response = nullptr);

    // 7. 恢复出厂设置
    bool resetToFactory(std::string *response = nullptr);

    // 8. 直接控制PWM指令（替代速度控制）- 无返回
    void setPWM(int m1, int m2, int m3, int m4);

    // 9. 上报编码器数据
    bool setEncoderUpload(bool total, bool realtime, bool speed, std::string *response = nullptr);

    // 10. 查询Flash变量
    bool readFlash(std::string *response = nullptr);

    // 11. 查询电池电量
    bool readBatteryVoltage(std::string *response = nullptr);

private:
    std::string port_;
    int baudrate_;
    int fd_; // 串口文件描述符

    bool sendCommand(const std::string &cmd, std::string *response, int timeout_ms = 500);
    bool openSerial();
    void closeSerial();
};

#endif // MOTOR_DRIVER_H