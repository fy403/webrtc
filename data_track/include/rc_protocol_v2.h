#ifndef RC_PROTOCOL_V2_H
#define RC_PROTOCOL_V2_H

#include <cstdint>
#include <array>
#include <cstring>
#include <cmath>
#include <iostream>

namespace RCProtocolV2 {
    // 协议常量
    constexpr uint8_t MAGIC1 = 0xAA;
    constexpr uint8_t MAGIC2 = 0x55;
    constexpr uint8_t CONTROL_MSG = 0x01;
    constexpr uint8_t HEARTBEAT_MSG = 0x02; // 心跳包类型
    constexpr size_t FRAME_SIZE = 67; // 2 + 1 + 16*4 = 67字节
    constexpr size_t HEADER_SIZE = 3; // MAGIC1 + MAGIC2 + TYPE
    constexpr size_t CHANNELS = 16;

    // 控制帧结构
    struct ControlFrame {
        uint8_t magic1; // 0xAA
        uint8_t magic2; // 0x55
        uint8_t type; // 0x01
        float channels[16]; // -1.0 ~ +1.0

        // 序列化
        void serialize(uint8_t *buffer) const {
            size_t offset = 0;
            buffer[offset++] = magic1;
            buffer[offset++] = magic2;
            buffer[offset++] = type;

            for (int i = 0; i < 16; i++) {
                // 将float32转为字节（大端序）
                uint32_t value;
                std::memcpy(&value, &channels[i], sizeof(float));
                buffer[offset++] = (value >> 24) & 0xFF;
                buffer[offset++] = (value >> 16) & 0xFF;
                buffer[offset++] = (value >> 8) & 0xFF;
                buffer[offset++] = value & 0xFF;
            }
        }

        // 反序列化
        bool deserialize(const uint8_t *buffer) {
            if (buffer[0] != MAGIC1 || buffer[1] != MAGIC2) {
                return false;
            }
            if (buffer[2] != CONTROL_MSG) {
                return false;
            }

            magic1 = buffer[0];
            magic2 = buffer[1];
            type = buffer[2];

            size_t offset = 3;
            for (int i = 0; i < 16; i++) {
                uint32_t value = (buffer[offset] << 24) |
                                 (buffer[offset + 1] << 16) |
                                 (buffer[offset + 2] << 8) |
                                 buffer[offset + 3];
                std::memcpy(&channels[i], &value, sizeof(float));
                offset += 4;
            }

            return true;
        }
    };

    // 解析控制帧
    inline bool parseControlFrame(const uint8_t *frame, size_t length, ControlFrame &outFrame) {
        if (length != FRAME_SIZE) {
            return false;
        }
        return outFrame.deserialize(frame);
    }

    // 解析帧并返回消息类型
    // 返回值: true=解析成功, false=解析失败
    // outFrame: 填充解析后的帧数据
    // msgType: 输出消息类型 (CONTROL_MSG 或 HEARTBEAT_MSG)
    inline bool parseFrameWithType(const uint8_t *frame, size_t length, ControlFrame &outFrame, uint8_t &msgType) {
        if (length != FRAME_SIZE) {
            return false;
        }
        msgType = frame[2];
        if (msgType == HEARTBEAT_MSG) {
            return true;
        }
        return outFrame.deserialize(frame);
    }
} // namespace RCProtocolV2

#endif // RC_PROTOCOL_V2_H
