#ifndef CONSTANTS_H
#define CONSTANTS_H

// Motor control constants
const int MOTOR_FRONT_BACK = 2; // Control forward/backward
const int MOTOR_LEFT_RIGHT = 4; // Control left/right turn

const int16_t NEUTRAL_PWM = 0;
const int16_t MAX_FORWARD_PWM = 3500;
const int16_t MAX_REVERSE_PWM = -3500;

// Message type definitions
const uint8_t MSG_SYSTEM_STATUS = 0x20;
const uint8_t MSG_KEY = 0x01;
const uint8_t MSG_EMERGENCY_STOP = 0x02;
const uint8_t MSG_CYCLE_THROTTLE = 0x03;
const uint8_t MSG_STOP_ALL = 0x04;
const uint8_t MSG_QUIT = 0x05;
const uint8_t MSG_PING = 0x10;

// Key mapping
const char KEY_NAMES[4][2] = {"w", "s", "a", "d"};

#endif // CONSTANTS_H
