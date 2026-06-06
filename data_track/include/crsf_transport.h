#ifndef CRSF_TRANSPORT_H
#define CRSF_TRANSPORT_H

#include "serial_port.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <vector>

/**
 * CRSFTransport - CRSF 协议底层通信层
 *
 * 职责：
 * - 独占物理串口（通过 SerialPort）
 * - 维护16通道共享缓冲区（channels_）
 * - 独立发送线程，每20ms从channels_打包完整CRSF frames并发送
 * - 提供线程安全的 setChannel() 接口供上层多driver共用
 *
 * 生命周期：由 MotorController 创建并管理（shared_ptr），注入给各上层driver
 */
class CRSFTransport {
public:
    /**
     * 构造函数
     * @param port CRSF 串口设备路径（如 /dev/ttyUSB0）
     */
    explicit CRSFTransport(const std::string& port);

    ~CRSFTransport();

    /**
     * 打开串口并启动发送线程
     * @return true=成功, false=失败
     */
    bool connect();

    /**
     * 停止发送线程并关闭串口
     */
    void disconnect();

    /**
     * 设置某个 CRSF 通道的值（线程安全）
     * @param ch_index 通道索引（0-based，对应 CRSF channel 1-16）
     * @param value 通道值（CRSF原始范围，如 172-1811）
     */
    void setChannel(uint8_t ch_index, uint16_t value);

    /**
     * 获取发送线程运行状态
     */
    bool isRunning() const;

private:
    std::string port_;
    std::unique_ptr<SerialPort> serial_;
    std::vector<uint16_t> channels_;    // 16 通道缓冲区，全部初始化为中位值
    std::mutex channels_mutex_;         // 保护 channels_ 的读写
    std::thread send_thread_;           // 发送线程
    std::atomic<bool> running_;         // 发送线程运行标志

    // CRSF 协议帧大小常量
    static constexpr size_t CRSF_FRAME_SIZE_MAX = 64;
    static constexpr uint8_t CRSF_FRAMETYPE_RC_CHANNELS_PACKED = 0x16;

    // CRSF 协议工厂方法
    uint8_t calculateCRC8(const uint8_t* data, size_t length);
    void packChannels11bit(uint8_t* buffer, const std::vector<uint16_t>& ch_values, int num_channels);
    void createChannelsFrame(uint8_t* buffer, size_t& length);

    // 发送线程主循环
    void sendThreadFunc();
};

#endif // CRSF_TRANSPORT_H
