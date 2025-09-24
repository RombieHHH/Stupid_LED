#pragma once
#include <stdint.h>

namespace Storage
{
    bool begin();
    void saveState();
    void loadState();

    // getters for initial loading in LedController
    const char *getSavedMode();
    int getSavedBlinkHz();
    int getSavedBreathePeriod();
    uint8_t getSavedBrightness();
}
