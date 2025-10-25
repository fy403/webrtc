#ifndef THROTTLE_LEVELS_H
#define THROTTLE_LEVELS_H

#include <string>

// Throttle level definition
struct ThrottleLevel
{
    int level;
    int speed_percent;
    const char *description;
};

extern const ThrottleLevel THROTTLE_LEVELS[];
extern const int THROTTLE_LEVEL_COUNT;

#endif // THROTTLE_LEVELS_H
