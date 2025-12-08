#ifndef CONSTANTS_H
#define CONSTANTS_H

// Motor control constants
const int MOTOR_FRONT_BACK = 2; // Control forward/backward
const int MOTOR_LEFT_RIGHT = 4; // Control left/right turn

const int16_t NEUTRAL_PWM = 0;
const int16_t MAX_FORWARD_PWM = 3500;
const int16_t MAX_REVERSE_PWM = -3500;

// Status frame message type
const uint8_t MSG_SYSTEM_STATUS = 0x20;

// SBUS protocol constants
const uint8_t SBUS_START_BYTE = 0x0F;
const uint8_t SBUS_END_BYTE = 0x00;
const size_t SBUS_FRAME_SIZE = 25;
const uint8_t SBUS_CHANNELS = 16;
const uint16_t SBUS_MIN = 172;
const uint16_t SBUS_CENTER = 992;
const uint16_t SBUS_MAX = 1811;

#endif // CONSTANTS_H
