#include "4g_tty.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <sys/select.h>
#include <sys/time.h>
#include <vector>
#include <sstream>

FourGTty::FourGTty()
    : serial_fd_(-1), device_("/dev/ttyACM0"), baudrate_(115200)
{
}

FourGTty::~FourGTty()
{
    close();
}

bool FourGTty::open(const std::string &device, int baudrate)
{
    device_ = device;
    baudrate_ = baudrate;

    // 以读写和非阻塞模式打开串口
    serial_fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd_ < 0)
    {
        if (debug_)
        {
            std::cerr << "无法打开串口设备: " << device_ << " - " << strerror(errno) << std::endl;
        }
        return false;
    }

    // 配置串口
    if (!configureSerialPort())
    {
        close();
        return false;
    }

    if (debug_)
    {
        std::cout << "成功打开串口: " << device_ << " 波特率: " << baudrate_ << std::endl;
    }

    return true;
}

void FourGTty::close()
{
    if (serial_fd_ >= 0)
    {
        ::close(serial_fd_);
        serial_fd_ = -1;
        if (debug_)
        {
            std::cout << "串口已关闭" << std::endl;
        }
    }
}

bool FourGTty::isOpen() const
{
    return (serial_fd_ >= 0);
}

bool FourGTty::configureSerialPort()
{
    struct termios tty;

    if (tcgetattr(serial_fd_, &tty) != 0)
    {
        if (debug_)
        {
            std::cerr << "获取串口属性失败: " << strerror(errno) << std::endl;
        }
        return false;
    }

    // 设置波特率
    speed_t speed = baudrateToConstant(baudrate_);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 控制模式设置
    tty.c_cflag &= ~PARENB;          // 无奇偶校验
    tty.c_cflag &= ~CSTOPB;          // 1个停止位
    tty.c_cflag &= ~CSIZE;           // 清除数据位掩码
    tty.c_cflag |= CS8;              // 8个数据位
    tty.c_cflag &= ~CRTSCTS;         // 无硬件流控
    tty.c_cflag |= (CREAD | CLOCAL); // 启用接收，忽略调制解调器状态

    // 本地模式设置
    tty.c_lflag &= ~ICANON; // 非规范模式
    tty.c_lflag &= ~ECHO;   // 禁用回显
    tty.c_lflag &= ~ECHOE;  // 禁用擦除
    tty.c_lflag &= ~ECHONL; // 禁用换行回显
    tty.c_lflag &= ~ISIG;   // 禁用信号字符

    // 输入模式设置
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // 禁用软件流控
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // 输出模式设置
    tty.c_oflag &= ~OPOST; // 原始输出
    tty.c_oflag &= ~ONLCR; // 不将换行转换为回车换行

    // 超时设置：立即返回
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0)
    {
        if (debug_)
        {
            std::cerr << "设置串口属性失败: " << strerror(errno) << std::endl;
        }
        return false;
    }

    // 清空缓冲区
    tcflush(serial_fd_, TCIOFLUSH);

    return true;
}

bool FourGTty::sendCommand(const std::string &command)
{
    if (!isOpen())
    {
        if (debug_)
        {
            std::cerr << "串口未打开" << std::endl;
        }
        return false;
    }

    std::string full_command = command + "\r";
    ssize_t bytes_written = write(serial_fd_, full_command.c_str(), full_command.length());

    if (bytes_written != static_cast<ssize_t>(full_command.length()))
    {
        if (debug_)
        {
            std::cerr << "发送命令失败: " << command << std::endl;
        }
        return false;
    }

    // 确保数据发送完成
    tcdrain(serial_fd_);

    if (debug_)
    {
        std::cout << "发送命令: " << command << std::endl;
    }

    return true;
}

std::vector<std::string> FourGTty::sendCommandWithResponse(const std::string &command, int timeout_ms)
{
    std::vector<std::string> response;

    if (!sendCommand(command))
    {
        return response;
    }

    readResponse(response, timeout_ms);
    return response;
}

int FourGTty::readResponse(std::vector<std::string> &response, int timeout_ms)
{
    if (!isOpen())
    {
        return -1;
    }

    response.clear();
    char buffer[256];
    std::string current_line;
    struct timeval tv;
    fd_set readfds;

    // 设置超时
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    while (true)
    {
        FD_ZERO(&readfds);
        FD_SET(serial_fd_, &readfds);

        int ret = select(serial_fd_ + 1, &readfds, NULL, NULL, &tv);

        if (ret == 0)
        {
            // 超时
            if (debug_)
            {
                std::cout << "读取响应超时" << std::endl;
            }
            break;
        }
        else if (ret < 0)
        {
            // 错误
            if (debug_)
            {
                std::cerr << "select错误: " << strerror(errno) << std::endl;
            }
            break;
        }

        if (FD_ISSET(serial_fd_, &readfds))
        {
            ssize_t bytes_read = read(serial_fd_, buffer, sizeof(buffer) - 1);

            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';

                if (debug_)
                {
                    std::cout << "收到数据: " << buffer;
                }

                // 处理接收到的数据
                for (ssize_t i = 0; i < bytes_read; i++)
                {
                    char c = buffer[i];
                    if (c == '\r')
                    {
                        continue; // 忽略回车
                    }
                    else if (c == '\n')
                    {
                        if (!current_line.empty())
                        {
                            response.push_back(current_line);
                            current_line.clear();
                        }
                    }
                    else
                    {
                        current_line += c;
                    }
                }

                // 检查是否收到最终响应
                if (!response.empty())
                {
                    std::string last_line = response.back();
                    if (last_line == "OK" || last_line.find("ERROR") != std::string::npos ||
                        last_line.find("+CME ERROR") != std::string::npos)
                    {
                        break;
                    }
                }
            }
            else if (bytes_read == 0)
            {
                // 无数据可读
                break;
            }
            else
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    if (debug_)
                    {
                        std::cerr << "读取错误: " << strerror(errno) << std::endl;
                    }
                    break;
                }
            }
        }
    }

    // 处理最后一行（如果没有以换行结束）
    if (!current_line.empty())
    {
        response.push_back(current_line);
    }

    return response.size();
}

// 特定AT命令封装
bool FourGTty::testAT(int timeout_ms)
{
    auto response = sendCommandWithResponse("AT", timeout_ms);
    for (const auto &line : response)
    {
        if (line == "OK")
        {
            return true;
        }
    }
    return false;
}

std::string FourGTty::getSignalQuality(int timeout_ms)
{
    auto response = sendCommandWithResponse("AT+CSQ", timeout_ms);
    for (const auto &line : response)
    {
        if (line.find("+CSQ:") != std::string::npos)
        {
            // 提取冒号后的内容
            size_t pos = line.find(":");
            if (pos != std::string::npos)
            {
                std::string value = line.substr(pos + 1);
                // 去除前后空格
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                return value;
            }
        }
    }
    return "UNKNOWN";
}

std::string FourGTty::getSimStatus(int timeout_ms)
{
    auto response = sendCommandWithResponse("AT+CPIN?", timeout_ms);
    for (const auto &line : response)
    {
        if (line.find("+CPIN:") != std::string::npos)
        {
            size_t pos = line.find(":");
            if (pos != std::string::npos)
            {
                std::string value = line.substr(pos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                return value;
            }
        }
    }
    return "UNKNOWN";
}

std::string FourGTty::getNetworkRegistration(int timeout_ms)
{
    auto response = sendCommandWithResponse("AT+CREG?", timeout_ms);
    for (const auto &line : response)
    {
        if (line.find("+CREG:") != std::string::npos)
        {
            size_t pos = line.find(":");
            if (pos != std::string::npos)
            {
                std::string value = line.substr(pos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                return value;
            }
        }
    }
    return "UNKNOWN";
}

std::string FourGTty::getModuleInfo(int timeout_ms)
{
    auto response = sendCommandWithResponse("ATI", timeout_ms);
    // 对于ATI命令，我们返回所有非ATI、非OK的行
    std::string result;
    for (const auto &line : response)
    {
        if (line != "ATI" && line != "OK" && !line.empty())
        {
            if (!result.empty())
                result += "; ";
            result += line;
        }
    }
    return result.empty() ? "UNKNOWN" : result;
}

int FourGTty::baudrateToConstant(int baudrate)
{
    switch (baudrate)
    {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
    case 460800:
        return B460800;
    case 500000:
        return B500000;
    case 576000:
        return B576000;
    case 921600:
        return B921600;
    case 1000000:
        return B1000000;
    case 1152000:
        return B1152000;
    case 1500000:
        return B1500000;
    case 2000000:
        return B2000000;
    case 2500000:
        return B2500000;
    case 3000000:
        return B3000000;
    case 3500000:
        return B3500000;
    case 4000000:
        return B4000000;
    default:
        return B115200;
    }
}