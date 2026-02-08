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
