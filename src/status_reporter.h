#pragma once
#include <stdint.h>

namespace StatusReporter
{
    void begin();
    void broadcast();
    void sendTo(int clientNum);
    void sendTo(uint8_t clientNum); // overload to be safe for callers using uint8_t
}
