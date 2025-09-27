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

    // 硬件配置
    constexpr int LED_PIN = 12; // gpio12
    constexpr int LEDC_CHANNEL = 0;
    constexpr int LEDC_FREQ = 5000;  // base PWM frequency
    constexpr int LEDC_RES_BITS = 8; // 8-bit resolution -> duty 0-255

    static Mode currentMode = MODE_BREATHE;
    static int blinkHz = 2;
    static int breathePeriod = 1500;
    static uint8_t brightness = 128; // max duty 0-255

    // 运行时状态
    static unsigned long lastMs = 0;
    static unsigned long lastToggleMs = 0;
    static bool blinkState = false;
    // saved state before entering breathe-wait so we can restore on reconnect
    static Mode savedModeBeforeWait = MODE_BREATHE;
    static int savedBlinkHzBeforeWait = 2;
    static int savedBreathePeriodBeforeWait = 1500;
    static uint8_t savedBrightnessBeforeWait = 128;
    static bool hasSavedBeforeWait = false;

    // 实际写入 PWM 占空比的助手函数
    static void applyDuty(uint8_t duty)
    {
        // ledcWrite 在已配置 ledcSetup 时期望 0..(2^bits-1) 的值
        ledcWrite(LEDC_CHANNEL, duty);
    }

    void begin()
    {
    // 配置 LEDC 用于 gpio12
        ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RES_BITS);
        ledcAttachPin(LED_PIN, LEDC_CHANNEL);
    // 初始化计时器
        lastMs = millis();
        lastToggleMs = lastMs;
        blinkState = false;

    // 从 Storage 中应用保存的状态（如果有）。保证重启后恢复闪烁频率与呼吸周期。
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

        // 应用保存的亮度值
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
            unsigned long period = 1000u / (unsigned long)blinkHz; // 计算周期（ms）
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
            // 计算相位 [0..1)
            if (breathePeriod <= 0)
            {
                applyDuty(0);
                break;
            }
            float phase = fmod((float)now, (float)breathePeriod) / (float)breathePeriod; // 0..1
            // 使用 (1 - cos(2*pi*phase))/2 来实现平滑呼吸曲线
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
        if (currentMode == MODE_ON)
            applyDuty(brightness);
        else if (currentMode == MODE_BLINK && blinkState)
            applyDuty(brightness);
    }

    void onClientConnected()
    {
        // 取消任何 breathe-wait 状态，并在有保存的前置状态时恢复它
        if (currentMode == MODE_BREATHE_WAIT)
        {
            if (hasSavedBeforeWait)
            {
                // 恢复之前保存的参数
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
                // 未保存前置状态：回到普通呼吸模式
                currentMode = MODE_BREATHE;
            }
        }
    }

    void enterBreatheWait()
    {
        // 进入 breathe-wait 前保存当前运行状态，以便在客户端重新连接时恢复
        if (!hasSavedBeforeWait && currentMode != MODE_BREATHE_WAIT)
        {
            savedModeBeforeWait = currentMode;
            savedBlinkHzBeforeWait = blinkHz;
            savedBreathePeriodBeforeWait = breathePeriod;
            savedBrightnessBeforeWait = brightness;
            hasSavedBeforeWait = true;
        }
        currentMode = MODE_BREATHE_WAIT;
        // 在等待模式下使用较快的小幅呼吸作为空闲视觉效果
        breathePeriod = 800;
        brightness = 255;
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
