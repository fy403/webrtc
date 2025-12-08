#include "message_handler.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

// Define static const members
const uint8_t MessageHandler::MAGIC1;
const uint8_t MessageHandler::MAGIC2;

MessageHandler::MessageHandler() {}

MessageHandler::~MessageHandler() {}

bool MessageHandler::parseSbusFrame(const uint8_t *frame, size_t length, SbusFrame &out_frame) const
{
    if (!frame || length != SBUS_FRAME_SIZE)
    {
        return false;
    }

    if (frame[0] != SBUS_START_BYTE || frame[SBUS_FRAME_SIZE - 1] != SBUS_END_BYTE)
    {
        return false;
    }

    out_frame.channels.fill(SBUS_CENTER);

    size_t bit_index = 0;
    for (size_t ch = 0; ch < SBUS_CHANNELS; ++ch)
    {
        uint16_t value = 0;
        for (int bit = 0; bit < 11; ++bit, ++bit_index)
        {
            const size_t byte_idx = 1 + (bit_index >> 3);
            const uint8_t bit_in_byte = (frame[byte_idx] >> (bit_index & 0x07)) & 0x01;
            value |= static_cast<uint16_t>(bit_in_byte) << bit;
        }
        out_frame.channels[ch] = value;
    }

    const uint8_t flags = frame[23];
    out_frame.frame_lost = (flags & 0x04) != 0;
    out_frame.failsafe = (flags & 0x08) != 0;
    out_frame.valid = true;
    return true;
}

double MessageHandler::sbusToNormalized(uint16_t value)
{
    value = std::min<uint16_t>(SBUS_MAX, std::max<uint16_t>(SBUS_MIN, value));
    if (value >= SBUS_CENTER)
    {
        return static_cast<double>(value - SBUS_CENTER) / static_cast<double>(SBUS_MAX - SBUS_CENTER);
    }
    else
    {
        return static_cast<double>(value - SBUS_CENTER) / static_cast<double>(SBUS_CENTER - SBUS_MIN);
    }
}

uint16_t MessageHandler::calculateChecksum(const uint8_t *frame, size_t length) const
{
    uint16_t checksum = 0;
    for (size_t i = 0; i < length; i++)
    {
        checksum += frame[i];
    }
    return checksum;
}

void MessageHandler::createStatusFrame(const std::map<std::string, std::string> &statusData,
                                       std::vector<uint8_t> &frame)
{
    // Build data string
    std::string data;
    for (const auto &pair : statusData)
    {
        data += pair.first + ":" + pair.second + "\r\n";
    }

    // Build complete frame
    frame.clear();

    // Frame header
    frame.push_back(MAGIC1);
    frame.push_back(MAGIC2);
    frame.push_back(MSG_SYSTEM_STATUS);

    // Data length (2 bytes)
    uint16_t data_len = data.length();
    frame.push_back((data_len >> 8) & 0xFF);
    frame.push_back(data_len & 0xFF);

    // Data content
    for (char c : data)
    {
        frame.push_back(static_cast<uint8_t>(c));
    }

    // Calculate checksum (from header to end of data)
    uint16_t checksum = calculateChecksum(frame.data(), frame.size());
    frame.push_back((checksum >> 8) & 0xFF);
    frame.push_back(checksum & 0xFF);
}
