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
    struct SbusFrame
    {
        std::array<uint16_t, SBUS_CHANNELS> channels{};
        bool frame_lost = false;
        bool failsafe = false;
        bool valid = false;
    };

    MessageHandler();
    ~MessageHandler();

    // SBUS handling
    bool parseSbusFrame(const uint8_t *frame, size_t length, SbusFrame &out_frame) const;
    static double sbusToNormalized(uint16_t value);

    // Status frame creation (kept independent of SBUS control)
    void createStatusFrame(const std::map<std::string, std::string> &statusData,
                           std::vector<uint8_t> &frame);

private:
    static const uint8_t MAGIC1 = 0xAA;
    static const uint8_t MAGIC2 = 0x55;

    uint16_t calculateChecksum(const uint8_t *frame, size_t length) const;
};

#endif // MESSAGE_HANDLER_H
