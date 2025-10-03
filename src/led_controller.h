#pragma once
#include <stdint.h>

namespace LedController
{
    void begin();
    void update();
    void setModeOn();
    void setModeOff();
    void setModeBlink(int hz);
    void setModeBreathe(int period_ms);
    void setBrightness(uint8_t duty);
    void onClientConnected();
    void enterBreatheWait();
    // 用于在客户端断开连接时进入 breathe-wait 状态的保存变量
    const char *getModeStr();
    int getBlinkHz();
    int getBreathePeriod();
    uint8_t getBrightness();
}
