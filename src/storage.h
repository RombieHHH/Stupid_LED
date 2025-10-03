#pragma once
#include <stdint.h>

namespace Storage
{
    bool begin();
    void saveState();
    void loadState();

    // 保存和加载 LED 控制器的状态
    const char *getSavedMode();
    int getSavedBlinkHz();
    int getSavedBreathePeriod();
    uint8_t getSavedBrightness();
}
