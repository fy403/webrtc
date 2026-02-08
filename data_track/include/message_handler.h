#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

#include <array>
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include "constants.h"

class MessageHandler
{
public:
    MessageHandler();
    ~MessageHandler();

    // Status frame creation (独立于控制协议)
    void createStatusFrame(const std::map<std::string, std::string> &statusData,
                           std::vector<uint8_t> &frame);

private:
    static const uint8_t MAGIC1 = 0xAA;
    static const uint8_t MAGIC2 = 0x55;

    uint16_t calculateChecksum(const uint8_t *frame, size_t length) const;
};

#endif // MESSAGE_HANDLER_H
