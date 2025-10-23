#include "throttle_levels.h"

// Throttle levels
const ThrottleLevel THROTTLE_LEVELS[] = {
    {0, 0, "Stop"},
    {1, 25, "Low speed"},
    {2, 50, "Medium speed"},
    {3, 75, "Fast"},
    {4, 100, "Full speed"}
};

const int THROTTLE_LEVEL_COUNT = sizeof(THROTTLE_LEVELS) / sizeof(ThrottleLevel);
