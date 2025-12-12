#include <iostream>
#include <fcntl.h>
#include <cstring>
#include <time.h>
#include <thread>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include "./hardware_config.h"
#include "./serial_port.h"

// CRSF协议相关定义
#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8
#define CRSF_ADDRESS_RADIO_TRANSMITTER 0xEA
#define CRSF_ADDRESS_RADIO_RECEIVER 0xEC
#define CRSF_FRAME_SIZE_MAX 64
#define CRSF_BAUDRATE 420000  // CRSF标准波特率

// CRSF帧类型定义
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16


// CRSF协议处理类
class CRSFController {
private:
    SerialPort serial;
    std::vector<uint16_t> channels;
    std::unique_ptr<ServoConfig> servo_config;
    std::unique_ptr<ESCConfig> esc_config;
    std::unique_ptr<CRSFConfig> crsf_config;
    std::thread send_thread;  // 后台发送线程
    std::atomic<bool> running;  // 线程运行标志
    std::mutex channels_mutex;  // 通道数据互斥锁
    bool debug_mode;  // 调试模式

    // 将舵机PWM脉宽转换为CRSF通道值
    uint16_t servoPwmToChannel(uint16_t pwm_us, const ServoConfig &servo_config, const CRSFConfig &crsf_config) {
        // PWM脉宽范围映射到CRSF范围
        float ratio = static_cast<float>(pwm_us - servo_config.min_pulse_width) /
                      (servo_config.max_pulse_width - servo_config.min_pulse_width);
        return static_cast<uint16_t>(crsf_config.channel_min +
                                     ratio * (crsf_config.channel_max - crsf_config.channel_min));
    }

    // 将电调PWM脉宽转换为CRSF通道值
    uint16_t escPwmToChannel(uint16_t pwm_us, const ESCConfig &esc_config, const CRSFConfig &crsf_config) {
        // PWM脉宽范围映射到CRSF范围
        float ratio = static_cast<float>(pwm_us - esc_config.min_pulse_width) /
                      (esc_config.max_pulse_width - esc_config.min_pulse_width);
        return static_cast<uint16_t>(crsf_config.channel_min +
                                     ratio * (crsf_config.channel_max - crsf_config.channel_min));
    }

    // 将角度转换为PWM脉宽
    uint16_t angleToPWM(float angle, ServoConfig &config) {
        // 角度范围映射到脉冲宽度范围
        float ratio = (angle - config.min_angle) / (config.max_angle - config.min_angle);
        return static_cast<uint16_t>(config.min_pulse_width +
                                     ratio * (config.max_pulse_width - config.min_pulse_width));
    }

    // CRC8计算 (多项式 0xD5)
    uint8_t calculateCRC8(const uint8_t *data, size_t length) {
        uint8_t crc = 0;
        for (size_t i = 0; i < length; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ 0xD5;
                } else {
                    crc = crc << 1;
                }
            }
        }
        return crc;
    }

    // 将通道值打包成11位格式
    void packChannels11bit(uint8_t *buffer, const std::vector<uint16_t> &ch_values, int num_channels) {
        // 清零缓冲区
        memset(buffer, 0, 22);

        int bit_index = 0;
        for (int i = 0; i < num_channels && i < 16; i++) {
            uint16_t value = (i < ch_values.size()) ? ch_values[i] : crsf_config->channel_neutral;
            // 确保值在11位范围内 (0-2047)
            value &= 0x07FF;

            // 将11位值打包到缓冲区
            for (int bit = 0; bit < 11; bit++) {
                int byte_idx = bit_index >> 3;
                int bit_in_byte = bit_index & 0x07;

                if (value & (1 << bit)) {
                    buffer[byte_idx] |= (1 << bit_in_byte);
                }
                bit_index++;
            }
        }
    }

    // 创建CRSF通道帧
    void createChannelsFrame(uint8_t *buffer, size_t &length) {
        buffer[0] = crsf_config->sync_byte; // 同步字节 0xC8
        buffer[1] = 24;                     // 数据长度: 22字节payload + 1字节type + 1字节CRC = 24
        buffer[2] = 0x16;                   // 通道数据帧类型 CRSF_FRAMETYPE_RC_CHANNELS_PACKED

        // 打包16个通道，每个通道11位，总共176位 = 22字节
        packChannels11bit(&buffer[3], channels, 16);

        // 计算CRC: 从type (buffer[2]) 到payload结束 (buffer[24])
        uint8_t crc = calculateCRC8(&buffer[2], 23); // type + 22字节payload
        buffer[25] = crc;

        length = 26;    // 整个帧长度: sync(1) + len(1) + type(1) + payload(22) + crc(1) = 26
    }

    void setThrottlePWM(uint16_t pwm_us) {
        uint16_t min_pw = esc_config->min_pulse_width;
        uint16_t max_pw = esc_config->max_pulse_width;

        if (pwm_us < min_pw)
            pwm_us = min_pw;
        if (pwm_us > max_pw)
            pwm_us = max_pw;

        // CRSF通道编号从1开始，数组索引从0开始，需要减1
        uint8_t ch_idx = esc_config->channel - 1;
        if (ch_idx < channels.size()) {
            std::lock_guard<std::mutex> lock(channels_mutex);
            channels[ch_idx] = escPwmToChannel(pwm_us, *esc_config, *crsf_config);
        }
    }

public:
    CRSFController(const std::string &port,
                   std::unique_ptr<ServoConfig> servo_cfg = nullptr,
                   std::unique_ptr<ESCConfig> esc_cfg = nullptr,
                   std::unique_ptr<CRSFConfig> crsf_cfg = nullptr)
            : serial(port),
              servo_config(std::move(servo_cfg)),
              esc_config(std::move(esc_cfg)),
              crsf_config(crsf_cfg ? std::move(crsf_cfg) : std::make_unique<CRSFConfig>()),
              running(false),
              debug_mode(false) {
        // 如果没有提供配置，则使用默认配置
        if (!servo_config) {
            servo_config = std::make_unique<ServoConfig>();
        }
        if (!esc_config) {
            esc_config = std::make_unique<ESCConfig>();
        }

        channels.resize(crsf_config->channel_count, crsf_config->channel_neutral);
    }

    ~CRSFController() {
        stop();
    }

    bool initialize() {
        // 打开CRSF串口（固定420000波特率）
        if (!serial.openPort()) {
            return false;
        }

        // 启动后台发送线程
        running = true;
        send_thread = std::thread(&CRSFController::sendThreadFunc, this);

        return true;
    }

    void stop() {
        if (running) {
            running = false;
            if (send_thread.joinable()) {
                send_thread.join();
            }
        }
    }

    // 后台发送线程函数 - 使用clock_nanosleep实现精确的20ms周期
    void sendThreadFunc() {
        uint8_t frame[CRSF_FRAME_SIZE_MAX];
        size_t length;

        // 使用CLOCK_MONOTONIC时钟源，不受系统时间调整影响
        const long period_nsec = 20000000;  // 20ms = 20000000 纳秒

        struct timespec next_time;
        // 获取当前时间作为起始点
        clock_gettime(CLOCK_MONOTONIC, &next_time);

        uint32_t send_count = 0;
        while (running) {
            // 构建并发送CRSF帧
            {
                std::lock_guard<std::mutex> lock(channels_mutex);
                createChannelsFrame(frame, length);
            }

            // 调试输出：显示发送的帧（每50帧输出一次，避免刷屏）
            if (debug_mode && (send_count % 50 == 0)) {
                std::cout << "[DEBUG] 发送CRSF通道帧 #" << send_count
                          << ", 长度: " << length << std::endl;
                std::cout << "[DEBUG] 帧数据: ";
                for (size_t i = 0; i < length; i++) {
                    printf("%02X ", frame[i]);
                }
                std::cout << std::endl;

                // 显示前几个通道的值
                std::cout << "[DEBUG] 通道值: ";
                for (size_t i = 0; i < channels.size() && i < 4; i++) {
                    std::cout << "CH" << (i + 1) << "=" << channels[i] << " ";
                }
                std::cout << std::endl;
            }

            bool write_success = serial.writeData(frame, length);
            if (!write_success && debug_mode) {
                std::cerr << "[ERROR] 发送CRSF帧失败！" << std::endl;
            }

            send_count++;

            // 计算下一次发送时间（绝对时间）
            next_time.tv_nsec += period_nsec;
            if (next_time.tv_nsec >= 1000000000) {
                next_time.tv_sec += next_time.tv_nsec / 1000000000;
                next_time.tv_nsec = next_time.tv_nsec % 1000000000;
            }

            // 使用clock_nanosleep精确等待到下一个周期（绝对时间模式）
            struct timespec remaining;
            int ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_time, &remaining);

            // 处理中断（EINTR）- 在绝对时间模式下，只需重新调用即可
            while (ret == EINTR && running) {
                ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_time, &remaining);
            }

            if (ret != 0 && ret != EINTR) {
                std::cerr << "clock_nanosleep错误: " << strerror(ret) << std::endl;
                break;
            }
        }
    }

    // 设置调试模式
    void setDebugMode(bool enable) {
        debug_mode = enable;
    }

    // 设置电调PWM值 (通道1)
    // 设置油门百分比 (-100% 到 +100%)
    void setThrottlePercent(float percent) {
        // 映射到电调脉冲宽度范围
        uint16_t min_pw = esc_config->min_pulse_width;
        uint16_t max_pw = esc_config->max_pulse_width;
        uint16_t neutral_pw = esc_config->neutral_pulse_width;

        uint16_t pwm_us;
        if (percent >= 0) {
            // 正向油门: 中位到最大
            pwm_us = neutral_pw + static_cast<uint16_t>((percent / 100.0f) * (max_pw - neutral_pw));
        } else {
            // 反向或刹车: 最小到中位 (取决于电调是否支持倒转)
            if (esc_config->reversible) {
                // 先计算有符号的偏移量，避免负数转换为无符号整数时出错
                float offset = (percent / 100.0f) * (neutral_pw - min_pw);
                pwm_us = static_cast<uint16_t>(neutral_pw + offset);
            } else {
                // 不支持倒转的电调，负值表示刹车/减速
                pwm_us = neutral_pw;
            }
        }
        setThrottlePWM(pwm_us);
    }

    // 设置舵机到特定角度
    void setServoToAngle(float angle) {
        if (angle < servo_config->min_angle)
            angle = servo_config->min_angle;
        if (angle > servo_config->max_angle)
            angle = servo_config->max_angle;

        uint16_t pwm_us = angleToPWM(angle, *servo_config);
        // CRSF通道编号从1开始，数组索引从0开始，需要减1
        uint8_t ch_idx = servo_config->channel - 1;
        if (ch_idx < channels.size()) {
            std::lock_guard<std::mutex> lock(channels_mutex);
            uint16_t channel_value = servoPwmToChannel(pwm_us, *servo_config, *crsf_config);
            channels[ch_idx] = channel_value;
            if (debug_mode) {
                std::cout << "[DEBUG] 设置通道" << (int) servo_config->channel
                          << " (索引" << (int) ch_idx << "): 角度=" << angle
                          << "° -> PWM=" << pwm_us << "us -> CRSF=" << channel_value << std::endl;
            }
        } else {
            std::cerr << "[ERROR] 通道索引超出范围: " << (int) ch_idx
                      << " >= " << channels.size() << std::endl;
        }
    }


    // 电调校准流程
    void calibrateESC() {
        // 1. 发送中立位脉冲 (通常是最大值用于校准)
        std::cout << "发送中立位脉冲 (" << esc_config->neutral_pulse_width << "us)..." << std::endl;
        setThrottlePWM(esc_config->neutral_pulse_width);
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        std::cout << "校准完成，听到滴滴提示音后可以开始控制" << std::endl;
    }
};

int main() {
    // 设置CRSF转接板的串口设备
    // 通常是 /dev/ttyUSB0 或 /dev/ttyACM0
    std::string serial_port = "/dev/ttyUSB1";

    // 舵机配置
    auto custom_servo = std::make_unique<ServoConfig>(
            500,   // 最小脉冲宽度
            2500,  // 最大脉冲宽度
            1500,  // 中位脉冲宽度
            0.0f,  // 最小角度
            180.0f,// 最大角度
            2,     // CRSF通道编号
            "High Torque Servo"
    );
    // 电调配置
    auto custom_esc = std::make_unique<ESCConfig>(
            900,   // 最小脉冲宽度
            2100,  // 最大脉冲宽度
            1500,  // 中位脉冲宽度
            true,  // 支持倒转
            1,     // CRSF通道编号
            "High Performance ESC"
    );

    // 创建自定义CRSF配置，包含通道数
    auto custom_crsf = std::make_unique<CRSFConfig>(
            0xC8,   // 同步字节
            172,    // 通道最小值
            992,    // 通道中位值
            1811,   // 通道最大值
            12      // 通道数量
    );

    CRSFController controller(serial_port, std::move(custom_servo), std::move(custom_esc), std::move(custom_crsf));

    // 初始化CRSF串口（固定420000波特率）
    std::cout << "正在初始化CRSF控制器（CRSF标准波特率: 420000 bps）..." << std::endl;
    if (!controller.initialize()) {
        std::cerr << "初始化失败，请检查串口连接" << std::endl;
        return 1;
    }

    std::cout << "CRSF控制器初始化成功" << std::endl;
    std::cout << "开始发送CRSF控制帧..." << std::endl;

    // 启用调试模式以查看详细输出
    controller.setDebugMode(true);

    // 等待一下让发送线程启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    controller.calibrateESC();
    // 运行控制测试
    std::cout << "\n=== 测试开始 ===\n"
              << std::endl;

    // 1. 舵机控制测试
    std::cout << "1. 测试舵机控制 (每个位置3秒):" << std::endl;

//    // 0度
//    std::cout << "设置舵机到0度" << std::endl;
//    controller.setServoToAngle(0);
//    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒
//
    // 45度
    std::cout << "设置舵机到45度" << std::endl;
    controller.setServoToAngle(45);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒
//
//    // 90度
//    std::cout << "设置舵机到90度" << std::endl;
//    controller.setServoToAngle(90);
//    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒
//
//    // 135度
//    std::cout << "设置舵机到135度" << std::endl;
//    controller.setServoToAngle(135);
//    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒
//
//    // 180度
//    std::cout << "设置舵机到180度" << std::endl;
//    controller.setServoToAngle(180);
//    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒

    // 2. 油门控制测试
    std::cout << "\n2. 测试油门控制 (每个油门级别3秒):" << std::endl;

    // 零油门
    std::cout << "设置油门到0%" << std::endl;
    controller.setThrottlePercent(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒

    // 50%正向油门
    std::cout << "设置油门到50%" << std::endl;
    controller.setThrottlePercent(50);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒

    // 100%正向油门
//    std::cout << "设置油门到100%" << std::endl;
//    controller.setThrottlePercent(100);
//    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒

//    std::cout << "\n返回中立位置" << std::endl;
//    controller.setThrottlePercent(0);
//    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 50%反向油门
    std::cout << "设置油门到-50%" << std::endl;
    controller.setThrottlePercent(-50);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒

//    // 100%反向油门
//    std::cout << "设置油门到-100%" << std::endl;
//    controller.setThrottlePercent(-100);
//    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 等待3秒


    // 返回中立位置
    std::cout << "\n返回中立位置" << std::endl;
    controller.setThrottlePercent(0);
    controller.setServoToAngle(90);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // 等待1秒

    // 显示最终状态
    std::cout << "\n=== 测试完成 ===" << std::endl;
    std::cout << "CRSF控制帧持续发送中..." << std::endl;

    return 0;
}