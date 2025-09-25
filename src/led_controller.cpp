#include "led_controller.h"
#include <Arduino.h>
#include "storage.h"
#include <cstring>

namespace LedController
{
    enum Mode
    {
        MODE_OFF,
        MODE_ON,
        MODE_BLINK,
        MODE_BREATHE,
        MODE_BREATHE_WAIT
    };

    // Hardware config
    constexpr int LED_PIN = 12; // gpio12
    constexpr int LEDC_CHANNEL = 0;
    constexpr int LEDC_FREQ = 5000;  // base PWM frequency
    constexpr int LEDC_RES_BITS = 8; // 8-bit resolution -> duty 0-255

    static Mode currentMode = MODE_BREATHE;
    static int blinkHz = 2;
    static int breathePeriod = 1500;
    static uint8_t brightness = 128; // max duty 0-255

    // runtime state
    static unsigned long lastMs = 0;
    static unsigned long lastToggleMs = 0;
    static bool blinkState = false;
    // saved state before entering breathe-wait so we can restore on reconnect
    static Mode savedModeBeforeWait = MODE_BREATHE;
    static int savedBlinkHzBeforeWait = 2;
    static int savedBreathePeriodBeforeWait = 1500;
    static uint8_t savedBrightnessBeforeWait = 128;
    static bool hasSavedBeforeWait = false;

    // helper to actually apply PWM duty
    static void applyDuty(uint8_t duty)
    {
        // ledcWrite expects 0..(2^bits-1) when configured with ledcSetup
        ledcWrite(LEDC_CHANNEL, duty);
    }

    void begin()
    {
        // configure LEDC for gpio12
        ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RES_BITS);
        ledcAttachPin(LED_PIN, LEDC_CHANNEL);
        // initialize timing
        lastMs = millis();
        lastToggleMs = lastMs;
        blinkState = false;

        // Apply saved state from Storage (if any). This ensures that
        // blink frequency and breathe period are restored after reboot.
        const char *m = Storage::getSavedMode();
        if (m && strcmp(m, "on") == 0)
        {
            setModeOn();
        }
        else if (m && strcmp(m, "off") == 0)
        {
            setModeOff();
        }
        else if (m && strcmp(m, "blink") == 0)
        {
            setModeBlink(Storage::getSavedBlinkHz());
        }
        else // default to breathe (or any other unrecognized value)
        {
            setModeBreathe(Storage::getSavedBreathePeriod());
        }

        // Apply saved brightness
        setBrightness(Storage::getSavedBrightness());
    }

    void update()
    {
        unsigned long now = millis();
        unsigned long dt = now - lastMs;
        lastMs = now;

        switch (currentMode)
        {
        case MODE_ON:
            applyDuty(brightness);
            break;
        case MODE_OFF:
            applyDuty(0);
            break;
        case MODE_BLINK:
        {
            if (blinkHz <= 0)
            {
                applyDuty(0);
                break;
            }
            unsigned long period = 1000u / (unsigned long)blinkHz; // ms per half-cycle? we'll toggle every period/2
            unsigned long half = period / 2;
            if (now - lastToggleMs >= half)
            {
                lastToggleMs = now;
                blinkState = !blinkState;
                applyDuty(blinkState ? brightness : 0);
            }
            break;
        }
        case MODE_BREATHE:
        case MODE_BREATHE_WAIT:
        {
            // compute phase [0..1)
            if (breathePeriod <= 0)
            {
                applyDuty(0);
                break;
            }
            float phase = fmod((float)now, (float)breathePeriod) / (float)breathePeriod; // 0..1
            // smooth breathe curve using (1 - cos(2*pi*phase))/2
            float val = (1.0f - cosf(2.0f * 3.14159265f * phase)) * 0.5f;
            // if in breathe wait, reduce amplitude slightly
            if (currentMode == MODE_BREATHE_WAIT)
                val *= 0.6f;
            uint8_t duty = (uint8_t)constrain((int)(val * (float)brightness), 0, 255);
            applyDuty(duty);
            break;
        }
        }
    }

    void setModeOn()
    {
        currentMode = MODE_ON;
    }

    void setModeOff()
    {
        currentMode = MODE_OFF;
    }

    void setModeBlink(int hz)
    {
        blinkHz = hz;
        currentMode = MODE_BLINK;
    }

    void setModeBreathe(int period_ms)
    {
        breathePeriod = period_ms;
        currentMode = MODE_BREATHE;
    }

    void setBrightness(uint8_t duty)
    {
        brightness = duty;
        // immediately apply new brightness for modes that use it
        if (currentMode == MODE_ON)
            applyDuty(brightness);
        else if (currentMode == MODE_BLINK && blinkState)
            applyDuty(brightness);
    }

    void onClientConnected()
    {
        // cancel any breathe-wait state and restore previous mode if we saved it
        if (currentMode == MODE_BREATHE_WAIT)
        {
            if (hasSavedBeforeWait)
            {
                // restore saved parameters first
                setBrightness(savedBrightnessBeforeWait);
                switch (savedModeBeforeWait)
                {
                case MODE_ON:
                    setModeOn();
                    break;
                case MODE_OFF:
                    setModeOff();
                    break;
                case MODE_BLINK:
                    setModeBlink(savedBlinkHzBeforeWait);
                    break;
                case MODE_BREATHE:
                default:
                    setModeBreathe(savedBreathePeriodBeforeWait);
                    break;
                }
                hasSavedBeforeWait = false;
            }
            else
            {
                // no saved state: just return to normal breathe
                currentMode = MODE_BREATHE;
            }
        }
    }

    void enterBreatheWait()
    {
        // when entering breathe-wait, save current runtime state so it can be
        // restored when a client reconnects
        if (!hasSavedBeforeWait && currentMode != MODE_BREATHE_WAIT)
        {
            savedModeBeforeWait = currentMode;
            savedBlinkHzBeforeWait = blinkHz;
            savedBreathePeriodBeforeWait = breathePeriod;
            savedBrightnessBeforeWait = brightness;
            hasSavedBeforeWait = true;
        }
        currentMode = MODE_BREATHE_WAIT;
    }

    const char *getModeStr()
    {
        switch (currentMode)
        {
        case MODE_ON:
            return "on";
        case MODE_OFF:
            return "off";
        case MODE_BLINK:
            return "blink";
        case MODE_BREATHE:
        case MODE_BREATHE_WAIT:
        default:
            return "breathe";
        }
    }

    int getBlinkHz()
    {
        return blinkHz;
    }

    int getBreathePeriod()
    {
        return breathePeriod;
    }

    uint8_t getBrightness()
    {
        return brightness;
    }
}
