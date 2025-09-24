#pragma once
#include <stdint.h>

namespace LedController
{
    void begin();
    void update(); // must be called frequently in loop()
    void setModeOn();
    void setModeOff();
    void setModeBlink(int hz);
    void setModeBreathe(int period_ms);
    void setBrightness(uint8_t duty);

    // called by websocket layer when client connects/disconnects
    void onClientConnected();
    void enterBreatheWait(); // when disconnected -> go breathe waiting
    // getters for status
    const char *getModeStr();
    int getBlinkHz();
    int getBreathePeriod();
    uint8_t getBrightness();
}
