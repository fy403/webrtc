#ifndef CRSF_MOTOR_DRIVER_H
#define CRSF_MOTOR_DRIVER_H

#include "motor_driver.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <vector>
#include <iostream>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <sys/ioctl.h>

/**
 * 舵机配置类
 * 用于配置不同类型的舵机参数
 */
class ServoConfig
{
public:
    // 舵机最小脉冲宽度 (单位: 微秒)
    uint16_t min_pulse_width;

    // 舵机最大脉冲宽度 (单位: 微秒)
    uint16_t max_pulse_width;

    // 舵机中位脉冲宽度 (单位: 微秒)
    uint16_t neutral_pulse_width;

    // 舵机最小角度
    float min_angle;

    // 舵机最大角度
    float max_angle;

    // 舵机名称
    std::string name;

    // 接CRSF通道编号
    uint8_t channel;

    /**
     * 默认构造函数 - 使用标准舵机参数 (900-2100us, 0-180度)
     */
    ServoConfig() : min_pulse_width(900),
                    max_pulse_width(2100),
                    neutral_pulse_width(1500),
                    min_angle(0.0f),
                    max_angle(180.0f),
                    channel(0),
                    name("Standard Servo") {}

    /**
     * 自定义构造函数
     * @param min_pw 最小脉冲宽度
     * @param max_pw 最大脉冲宽度
     * @param neutral_pw 中位脉冲宽度
     * @param min_ang 最小角度
     * @param max_ang 最大角度
     * @param servo_name 舵机名称
     */
    ServoConfig(uint16_t min_pw, uint16_t max_pw, uint16_t neutral_pw,
                float min_ang, float max_ang, uint8_t servo_channel, const std::string &servo_name) : min_pulse_width(min_pw),
                                                                                                      max_pulse_width(max_pw),
                                                                                                      neutral_pulse_width(neutral_pw),
                                                                                                      min_angle(min_ang),
                                                                                                      max_angle(max_ang),
                                                                                                      channel(servo_channel),
                                                                                                      name(servo_name) {}
};

/**
 * 电调配置类
 * 用于配置不同类型的电调参数
 */
class ESCConfig
{
public:
    // 电调最小脉冲宽度 (单位: 微秒)
    uint16_t min_pulse_width;

    // 电调最大脉冲宽度 (单位: 微秒)
    uint16_t max_pulse_width;

    // 电调中位/停止脉冲宽度 (单位: 微秒)
    uint16_t neutral_pulse_width;

    // 是否支持倒转 (反向驱动)
    bool reversible;

    // 电调名称
    std::string name;

    // CRSF通道编号
    uint8_t channel;

    /**
     * 默认构造函数 - 使用标准电调参数 (1000-2000us)
     */
    ESCConfig() : min_pulse_width(1000),
                  max_pulse_width(2000),
                  neutral_pulse_width(1500),
                  reversible(false),
                  channel(1),
                  name("Standard ESC") {}

    /**
     * 自定义构造函数
     * @param min_pw 最小脉冲宽度
     * @param max_pw 最大脉冲宽度
     * @param neutral_pw 中位脉冲宽度
     * @param is_reversible 是否支持倒转
     * @param esc_name 电调名称
     */
    ESCConfig(uint16_t min_pw, uint16_t max_pw, uint16_t neutral_pw,
              bool is_reversible, uint8_t esc_channel, const std::string &esc_name) : min_pulse_width(min_pw),
                                                                                      max_pulse_width(max_pw),
                                                                                      neutral_pulse_width(neutral_pw),
                                                                                      reversible(is_reversible),
                                                                                      channel(esc_channel),
                                                                                      name(esc_name) {}
};

/**
 * CRSF协议配置类
 * 用于配置CRSF协议相关参数
 */
class CRSFConfig
{
public:
    // CRSF协议同步字节
    uint8_t sync_byte;

    // CRSF通道最小值
    uint16_t channel_min;

    // CRSF通道中位值
    uint16_t channel_neutral;

    // CRSF通道最大值
    uint16_t channel_max;

    // CRSF通道数量
    uint8_t channel_count;

    /**
     * 默认构造函数 - 使用标准CRSF参数
     */
    CRSFConfig() : sync_byte(0xC8),
                   channel_min(172),
                   channel_neutral(992),
                   channel_max(1811),
                   channel_count(12) {}

    /**
     * 自定义构造函数
     * @param sync 同步字节
     * @param ch_min 通道最小值
     * @param ch_neutral 通道中位值
     * @param ch_max 通道最大值
     * @param ch_count 通道数量
     */
    CRSFConfig(uint8_t sync, uint16_t ch_min, uint16_t ch_neutral, uint16_t ch_max, uint8_t ch_count) : sync_byte(sync),
                                                                                                        channel_min(ch_min),
                                                                                                        channel_neutral(ch_neutral),
                                                                                                        channel_max(ch_max),
                                                                                                        channel_count(ch_count) {}
};

// 定义termios2结构和相关常量
#ifndef TERMIOS2_DEFINED
#define TERMIOS2_DEFINED
struct termios2
{
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
class SerialPort
{
private:
    int fd;
    std::string port;
    static const int CRSF_BAUDRATE = 420000;

public:
    SerialPort(const std::string &port_name) : port(port_name), fd(-1) {}

    // 打开CRSF串口（固定420000波特率）
    bool openPort()
    {
        // 以读写和非阻塞模式打开串口（参考代码使用O_NDELAY | O_NONBLOCK）
        fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
        if (fd == -1)
        {
            std::cerr << "无法打开串口设备: " << port << " - " << strerror(errno) << std::endl;
            return false;
        }

        // 读取原来的配置信息（参考代码的做法）
        struct termios2 oldtio, newtio;
        if (ioctl(fd, TCGETS2, &oldtio) != 0)
        {
            std::cerr << "获取串口属性失败: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
            close(fd);
            fd = -1;
            return false;
        }

        // 新结构体清零（参考代码使用memset清零）
        memset(&newtio, 0, sizeof(newtio));

        // CRSF标准配置：数据位8，停止位1，校验位none
        newtio.c_cflag |= (CLOCAL | CREAD); // 启用接收，忽略调制解调器状态
        newtio.c_cflag &= ~CSIZE;           // 清除数据位掩码
        newtio.c_cflag |= CS8;              // 8个数据位
        newtio.c_cflag &= ~PARENB;          // 无奇偶校验
        newtio.c_cflag &= ~CSTOPB;          // 1个停止位
        newtio.c_cflag &= ~CRTSCTS;         // 无硬件流控

        // 设置CRSF标准波特率420000 - 非标准波特率使用BOTHER模式（参考代码方法）
        newtio.c_cflag |= BOTHER;        // 使用自定义波特率
        newtio.c_ispeed = CRSF_BAUDRATE; // 输入波特率 420000
        newtio.c_ospeed = CRSF_BAUDRATE; // 输出波特率 420000

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
        newtio.c_cc[VTIME] = 10; // 1秒超时（10 * 0.1秒）

        // 清空输入缓冲区（参考代码在设置前清空）
        tcflush(fd, TCIFLUSH);

        if (ioctl(fd, TCSETS2, &newtio) != 0)
        {
            std::cerr << "设置CRSF串口属性失败: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
            close(fd);
            fd = -1;
            return false;
        }

        // 验证设置是否成功
        struct termios2 verify_tty;
        if (ioctl(fd, TCGETS2, &verify_tty) == 0)
        {
            if (verify_tty.c_ispeed != CRSF_BAUDRATE || verify_tty.c_ospeed != CRSF_BAUDRATE)
            {
                std::cerr << "警告: CRSF波特率设置验证失败！" << std::endl;
                std::cerr << "期望: " << CRSF_BAUDRATE << ", 实际输入: " << verify_tty.c_ispeed
                          << ", 实际输出: " << verify_tty.c_ospeed << std::endl;
            }
            else
            {
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

    bool writeData(const uint8_t *data, size_t length)
    {
        if (fd == -1)
        {
            std::cerr << "[ERROR] 串口未打开，无法写入数据" << std::endl;
            return false;
        }

        ssize_t written = write(fd, data, length);
        if (written != static_cast<ssize_t>(length))
        {
            std::cerr << "[ERROR] 写入数据不完整: 期望=" << length
                      << "字节, 实际=" << written << "字节" << std::endl;
            return false;
        }

        // 确保数据发送完成
        tcdrain(fd);

        return true;
    }

    int getFd() const
    {
        return fd;
    }

    void closePort()
    {
        if (fd != -1)
        {
            close(fd);
            fd = -1;
        }
    }

    ~SerialPort()
    {
        closePort();
    }

    // 验证CRSF波特率设置
    bool verifyBaudrate()
    {
        if (fd == -1)
        {
            std::cerr << "串口未打开，无法验证波特率" << std::endl;
            return false;
        }

        struct termios2 tty;
        if (ioctl(fd, TCGETS2, &tty) != 0)
        {
            std::cerr << "无法读取串口属性进行验证: " << strerror(errno) << std::endl;
            return false;
        }
        std::cout << "当前CRSF波特率设置 - 输入: " << tty.c_ispeed
                  << " bps, 输出: " << tty.c_ospeed << " bps" << std::endl;
        if (tty.c_cflag & BOTHER)
        {
            std::cout << "使用自定义波特率模式 (BOTHER)" << std::endl;
        }
        return (tty.c_ispeed == CRSF_BAUDRATE && tty.c_ospeed == CRSF_BAUDRATE);
    }
};

/**
 * CRSF Motor Driver
 * 基于 CRSF 协议的电机驱动实现
 * - 使用舵机控制方向（左右转向）
 * - 使用电调控制前后（前进/后退）
 */
class CRSFMotorDriver : public MotorDriver
{
public:
    /**
     * 构造函数
     * @param port CRSF 串口设备路径
     * @param servo_min_pulse 舵机最小脉冲宽度（微秒）
     * @param servo_max_pulse 舵机最大脉冲宽度（微秒）
     * @param servo_neutral_pulse 舵机中位脉冲宽度（微秒）
     * @param servo_min_angle 舵机最小角度（度）
     * @param servo_max_angle 舵机最大角度（度）
     * @param servo_channel CRSF 舵机通道编号（1-16）
     * @param esc_min_pulse 电调最小脉冲宽度（微秒）
     * @param esc_max_pulse 电调最大脉冲宽度（微秒）
     * @param esc_neutral_pulse 电调中位脉冲宽度（微秒）
     * @param esc_reversible 电调是否支持倒转
     * @param esc_channel CRSF 电调通道编号（1-16）
     */
    CRSFMotorDriver(const std::string &port,
                    uint16_t servo_min_pulse = 500,
                    uint16_t servo_max_pulse = 2500,
                    uint16_t servo_neutral_pulse = 1500,
                    float servo_min_angle = 0.0f,
                    float servo_max_angle = 180.0f,
                    uint8_t servo_channel = 2,
                    uint16_t esc_min_pulse = 900,
                    uint16_t esc_max_pulse = 2100,
                    uint16_t esc_neutral_pulse = 1500,
                    bool esc_reversible = true,
                    uint8_t esc_channel = 1);

    ~CRSFMotorDriver();

    // 实现 MotorDriver 接口
    bool connect() override;
    void disconnect() override;
    void setMotorPercent(int motor_id, int percent) override;
    void setFrontBackPercent(int percent) override;
    void setLeftRightPercent(int percent) override;

private:
    std::string port_;
    std::unique_ptr<SerialPort> serial_;
    std::unique_ptr<ServoConfig> servo_config_;
    std::unique_ptr<ESCConfig> esc_config_;
    std::unique_ptr<CRSFConfig> crsf_config_;

    std::vector<uint16_t> channels_;
    std::thread send_thread_;
    std::atomic<bool> running_;
    std::mutex channels_mutex_;

    // CRSF 协议相关方法
    uint8_t calculateCRC8(const uint8_t *data, size_t length);
    void packChannels11bit(uint8_t *buffer, const std::vector<uint16_t> &ch_values, int num_channels);
    void createChannelsFrame(uint8_t *buffer, size_t &length);
    void sendThreadFunc();

    // 转换方法
    uint16_t servoPwmToChannel(uint16_t pwm_us);
    uint16_t escPwmToChannel(uint16_t pwm_us);
    uint16_t angleToPWM(float angle);
    void setThrottlePWM(uint16_t pwm_us);
    void setServoAngle(float angle);
};

#endif // CRSF_MOTOR_DRIVER_H
