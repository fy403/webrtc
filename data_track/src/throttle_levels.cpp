#include "throttle_levels.h"

// Throttle levels
const ThrottleLevel THROTTLE_LEVELS[] = {
    {0, 0, "Stop"},
    {1, 40, "Low speed"},
    {2, 80, "Fast"},
    {3, 100, "Full speed"},
};

const int THROTTLE_LEVEL_COUNT = sizeof(THROTTLE_LEVELS) / sizeof(ThrottleLevel);
