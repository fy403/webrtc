#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <iostream>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/serial.h>
#include <linux/ioctl.h>

// 定义termios2结构和相关常量
#ifndef TERMIOS2_DEFINED
#define TERMIOS2_DEFINED
struct termios2 {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[19];
    speed_t c_ispeed;
    speed_t c_ospeed;
};
#endif // TERMIOS2_DEFINED

#ifndef BOTHER
#define BOTHER 0010000
#endif
#ifndef CBAUDEX
#define CBAUDEX 0010000
#endif
// 使用 _IOR 和 _IOW 宏定义 ioctl 命令
#ifndef TCGETS2
#define TCGETS2 _IOR('T', 0x2A, struct termios2)
#endif
#ifndef TCSETS2
#define TCSETS2 _IOW('T', 0x2B, struct termios2)
#endif

// CRSF串口配置类 - 固定420000波特率，8数据位，1停止位，无校验
class SerialPort {
private:
    int fd;
    std::string port;
    static const int CRSF_BAUDRATE = 420000;

public:
    SerialPort(const std::string &port_name) : port(port_name), fd(-1) {}

    // 打开CRSF串口（固定420000波特率）
    bool openPort() {
        // 以读写和非阻塞模式打开串口（参考代码使用O_NDELAY | O_NONBLOCK）
        fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
        if (fd == -1) {
            std::cerr << "无法打开串口设备: " << port << " - " << strerror(errno) << std::endl;
            return false;
        }

        // 读取原来的配置信息（参考代码的做法）
        struct termios2 oldtio, newtio;
        if (ioctl(fd, TCGETS2, &oldtio) != 0) {
            std::cerr << "获取串口属性失败: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
            close(fd);
            fd = -1;
            return false;
        }

        // 新结构体清零（参考代码使用memset清零）
        memset(&newtio, 0, sizeof(newtio));

        // CRSF标准配置：数据位8，停止位1，校验位none
        newtio.c_cflag |= (CLOCAL | CREAD);  // 启用接收，忽略调制解调器状态
        newtio.c_cflag &= ~CSIZE;            // 清除数据位掩码
        newtio.c_cflag |= CS8;               // 8个数据位
        newtio.c_cflag &= ~PARENB;           // 无奇偶校验
        newtio.c_cflag &= ~CSTOPB;           // 1个停止位
        newtio.c_cflag &= ~CRTSCTS;          // 无硬件流控

        // 设置CRSF标准波特率420000 - 非标准波特率使用BOTHER模式（参考代码方法）
        newtio.c_cflag |= BOTHER;            // 使用自定义波特率
        newtio.c_ispeed = CRSF_BAUDRATE;     // 输入波特率 420000
        newtio.c_ospeed = CRSF_BAUDRATE;     // 输出波特率 420000

        // 本地模式设置
        newtio.c_lflag &= ~ICANON; // 非规范模式
        newtio.c_lflag &= ~ECHO;   // 禁用回显
        newtio.c_lflag &= ~ECHOE;  // 禁用擦除
        newtio.c_lflag &= ~ECHONL; // 禁用换行回显
        newtio.c_lflag &= ~ISIG;   // 禁用信号字符

        // 输入模式设置
        newtio.c_iflag &= ~(IXON | IXOFF | IXANY); // 禁用软件流控
        newtio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

        // 输出模式设置
        newtio.c_oflag &= ~OPOST; // 原始输出
        newtio.c_oflag &= ~ONLCR; // 不将换行转换为回车换行

        // 超时设置
        newtio.c_cc[VMIN] = 0;
        newtio.c_cc[VTIME] = 10;  // 1秒超时（10 * 0.1秒）

        // 清空输入缓冲区（参考代码在设置前清空）
        tcflush(fd, TCIFLUSH);

        if (ioctl(fd, TCSETS2, &newtio) != 0) {
            std::cerr << "设置CRSF串口属性失败: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
            close(fd);
            fd = -1;
            return false;
        }

        // 验证设置是否成功
        struct termios2 verify_tty;
        if (ioctl(fd, TCGETS2, &verify_tty) == 0) {
            if (verify_tty.c_ispeed != CRSF_BAUDRATE || verify_tty.c_ospeed != CRSF_BAUDRATE) {
                std::cerr << "警告: CRSF波特率设置验证失败！" << std::endl;
                std::cerr << "期望: " << CRSF_BAUDRATE << ", 实际输入: " << verify_tty.c_ispeed
                          << ", 实际输出: " << verify_tty.c_ospeed << std::endl;
            } else {
                std::cout << "CRSF波特率设置验证成功: " << verify_tty.c_ispeed << " bps" << std::endl;
            }
        }

        // 清空缓冲区
        tcflush(fd, TCIOFLUSH);

        std::cout << "CRSF串口 " << port << " 打开成功" << std::endl;
        std::cout << "  波特率: " << CRSF_BAUDRATE << " bps" << std::endl;
        std::cout << "  数据位: 8, 停止位: 1, 校验位: none" << std::endl;
        return true;
    }

    bool writeData(const uint8_t *data, size_t length) {
        if (fd == -1) {
            std::cerr << "[ERROR] 串口未打开，无法写入数据" << std::endl;
            return false;
        }

        ssize_t written = write(fd, data, length);
        if (written != static_cast<ssize_t>(length)) {
            std::cerr << "[ERROR] 写入数据不完整: 期望=" << length
                      << "字节, 实际=" << written << "字节" << std::endl;
            return false;
        }

        // 确保数据发送完成
        tcdrain(fd);

        return true;
    }


    int getFd() const {
        return fd;
    }

    void closePort() {
        if (fd != -1) {
            close(fd);
            fd = -1;
        }
    }

    ~SerialPort() {
        closePort();
    }

    // 验证CRSF波特率设置
    bool verifyBaudrate() {
        if (fd == -1) {
            std::cerr << "串口未打开，无法验证波特率" << std::endl;
            return false;
        }

        struct termios2 tty;
        if (ioctl(fd, TCGETS2, &tty) != 0) {
            std::cerr << "无法读取串口属性进行验证: " << strerror(errno) << std::endl;
            return false;
        }
        std::cout << "当前CRSF波特率设置 - 输入: " << tty.c_ispeed
                  << " bps, 输出: " << tty.c_ospeed << " bps" << std::endl;
        if (tty.c_cflag & BOTHER) {
            std::cout << "使用自定义波特率模式 (BOTHER)" << std::endl;
        }
        return (tty.c_ispeed == CRSF_BAUDRATE && tty.c_ospeed == CRSF_BAUDRATE);
    }
};

#endif //SERIAL_PORT_H
