#include "message_handler.h"
#include <iostream>
#include <cstring>

// Define static const members
const uint8_t MessageHandler::MAGIC1;
const uint8_t MessageHandler::MAGIC2;
const size_t MessageHandler::FRAME_SIZE;

MessageHandler::MessageHandler()
{
}

MessageHandler::~MessageHandler()
{
}

bool MessageHandler::validateFrame(const uint8_t *frame)
{
    if (!frame) return false;
    
    // Check magic numbers
    if (frame[0] != MAGIC1 || frame[1] != MAGIC2) {
        std::cerr << "Invalid magic number" << std::endl;
        return false;
    }
    
    // Calculate and verify checksum
    uint16_t checksum = (frame[6] << 8) | frame[7];
    uint16_t calc = 0;
    for (int i = 0; i < 6; i++) {
        calc += frame[i];
    }
    
    if (checksum != calc) {
        std::cerr << "Checksum error: expected " << checksum << ", calculated " << calc << std::endl;
        return false;
    }
    
    return true;
}

uint16_t MessageHandler::calculateChecksum(const uint8_t *frame, size_t length)
{
    uint16_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum += frame[i];
    }
    return checksum;
}

void MessageHandler::createStatusFrame(const std::map<std::string, std::string> &statusData, 
                                      std::vector<uint8_t> &frame)
{
    // Build data string
    std::string data;
    for (const auto &pair : statusData) {
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
    for (char c : data) {
        frame.push_back(static_cast<uint8_t>(c));
    }

    // Calculate checksum (from header to end of data)
    uint16_t checksum = calculateChecksum(frame.data(), frame.size());
    frame.push_back((checksum >> 8) & 0xFF);
    frame.push_back(checksum & 0xFF);
}

MessageHandler::ParsedFrame MessageHandler::parseFrame(const uint8_t *frame)
{
    ParsedFrame result;
    result.valid = false;
    
    if (!validateFrame(frame)) {
        return result;
    }
    
    result.message_type = frame[2];
    result.key_code = frame[3];
    result.value = frame[4];
    result.valid = true;
    
    return result;
}
