#include "uart_motor_driver.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdlib> // For exit()

UartMotorDriver::UartMotorDriver(const std::string &port, int baudrate)
    : port_(port), baudrate_(baudrate), fd_(-1), error_count_(0) {}

UartMotorDriver::~UartMotorDriver()
{
    disconnect();
}

bool UartMotorDriver::connect()
{
    return openSerial();
}

void UartMotorDriver::disconnect()
{
    closeSerial();
}

void UartMotorDriver::checkErrorCount()
{
    if (error_count_ >= 4) {
        std::cerr << "Too many consecutive errors (" << error_count_ << "), exiting program." << std::endl;
        exit(1);
    }
}

bool UartMotorDriver::openSerial()
{
    fd_ = open(port_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0)
    {
        std::cerr << "Error opening serial port: " << port_ << std::endl;
        error_count_++;
        checkErrorCount();
        return false;
    }
    else
    {
        error_count_ = 0; // Reset error count on successful connection
    }

    struct termios tty;
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd_, &tty) != 0)
    {
        std::cerr << "Error getting termios attributes" << std::endl;
        error_count_++;
        checkErrorCount();
        return false;
    }

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0)
    {
        std::cerr << "Error setting termios attributes" << std::endl;
        error_count_++;
        checkErrorCount();
        return false;
    }
    else
    {
        error_count_ = 0; // Reset error count on successful setup
    }

    std::cout << "Serial port opened: " << port_ << std::endl;
    return true;
}

void UartMotorDriver::closeSerial()
{
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
        std::cout << "Serial port closed." << std::endl;
    }
}

bool UartMotorDriver::sendCommand(const std::string &cmd, std::string *response, int timeout_ms)
{
    if (fd_ < 0)
    {
        std::cerr << "Serial port not open." << std::endl;
        error_count_++;
        checkErrorCount();
        return false;
    }

    // 清空输入缓冲区
    tcflush(fd_, TCIFLUSH);

    std::string fullCmd = cmd + "\r\n";
    int n = write(fd_, fullCmd.c_str(), fullCmd.length());
    if (n < 0)
    {
        std::cerr << "Write error" << std::endl;
        error_count_++;
        checkErrorCount();
        return false;
    }
    else
    {
        error_count_ = 0; // Reset error count on successful write
    }

    std::cout << "Sent: " << cmd << std::endl;

    // 如果不需要响应，直接返回成功
    if (response == nullptr)
    {
        return true;
    }

    // 等待响应
    auto start = std::chrono::steady_clock::now();
    std::string resp;
    char buffer[256];

    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < timeout_ms)
    {

        int len = read(fd_, buffer, sizeof(buffer) - 1);
        if (len > 0)
        {
            buffer[len] = '\0';
            resp += buffer;

            // 检查是否包含OK（成功响应）
            if (resp.find("OK") != std::string::npos)
            {
                *response = resp;
                std::cout << "Response: " << resp << std::endl;
                error_count_ = 0; // Reset error count on successful response
                return true;
            }

            // 检查是否包含特定响应格式（如电池电量）
            if (resp.find("$") != std::string::npos && resp.find("#") != std::string::npos)
            {
                *response = resp;
                std::cout << "Response: " << resp << std::endl;
                error_count_ = 0; // Reset error count on successful response
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "Timeout or no valid response. Received: " << resp << std::endl;
    *response = resp;
    error_count_++; // Increment error count for timeout/no valid response
    checkErrorCount();
    return false;
}

// 1. 配置电机类型
bool UartMotorDriver::setMotorType(int type, std::string *response)
{
    std::string cmd = "$mtype:" + std::to_string(type) + "#";
    return sendCommand(cmd, response);
}

// 2. 配置电机死区
bool UartMotorDriver::setDeadZone(int deadzone, std::string *response)
{
    std::string cmd = "$deadzone:" + std::to_string(deadzone) + "#";
    return sendCommand(cmd, response);
}

// 3. 配置电机相位线
bool UartMotorDriver::setMotorLine(int lines, std::string *response)
{
    std::string cmd = "$mline:" + std::to_string(lines) + "#";
    return sendCommand(cmd, response);
}

// 4. 配置电机减速比
bool UartMotorDriver::setMotorPhase(int phase, std::string *response)
{
    std::string cmd = "$mphase:" + std::to_string(phase) + "#";
    return sendCommand(cmd, response);
}

// 5. 配置轮子直径
bool UartMotorDriver::setWheelDiameter(int diameter, std::string *response)
{
    std::string cmd = "$wdiameter:" + std::to_string(diameter) + "#";
    return sendCommand(cmd, response);
}

// 6. 配置PID参数
bool UartMotorDriver::setPID(float p, float i, float d, std::string *response)
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "$MPID:%.2f,%.2f,%.2f#", p, i, d);
    return sendCommand(buffer, response);
}

// 7. 恢复出厂设置
bool UartMotorDriver::resetToFactory(std::string *response)
{
    return sendCommand("$flash_reset#", response);
}

// 8. 直接控制PWM指令 - 无返回
void UartMotorDriver::setPWM(int m1, int m2, int m3, int m4)
{
    std::string cmd = "$pwm:" + std::to_string(m1) + "," + std::to_string(m2) + "," +
                      std::to_string(m3) + "," + std::to_string(m4) + "#";
    sendCommand(cmd, nullptr); // nullptr表示不需要响应
}

// 9. 上报编码器数据
bool UartMotorDriver::setEncoderUpload(bool total, bool realtime, bool speed, std::string *response)
{
    int t = total ? 1 : 0;
    int r = realtime ? 1 : 0;
    int s = speed ? 1 : 0;
    std::string cmd = "$upload:" + std::to_string(t) + "," + std::to_string(r) + "," + std::to_string(s) + "#";
    return sendCommand(cmd, response);
}

// 10. 查询Flash变量
bool UartMotorDriver::readFlash(std::string *response)
{
    return sendCommand("$read_flash#", response);
}

// 11. 查询电池电量
bool UartMotorDriver::readBatteryVoltage(std::string *response)
{
    return sendCommand("$read_vol#", response);
}

