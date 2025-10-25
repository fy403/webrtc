#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

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

    // Frame validation
    bool validateFrame(const uint8_t *frame);
    uint16_t calculateChecksum(const uint8_t *frame, size_t length);

    // Status frame creation
    void createStatusFrame(const std::map<std::string, std::string> &statusData, 
                          std::vector<uint8_t> &frame);
    
    // Frame parsing
    struct ParsedFrame {
        uint8_t message_type;
        uint8_t key_code;
        uint8_t value;
        bool valid;
    };
    
    ParsedFrame parseFrame(const uint8_t *frame);

private:
    static const uint8_t MAGIC1 = 0xAA;
    static const uint8_t MAGIC2 = 0x55;
    static const size_t FRAME_SIZE = 8;
};

#endif // MESSAGE_HANDLER_H
