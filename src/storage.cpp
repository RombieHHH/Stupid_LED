#include "storage.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include "led_controller.h"

// simple in-memory cached values
static char savedMode[16] = "breathe";
static int savedBlinkHz = 2;
static int savedBreathePeriod = 1500;
static uint8_t savedBrightness = 128;

bool Storage::begin()
{
    if (!SPIFFS.begin(true))
    {
        return false;
    }
    // try load existing state
    loadState();
    return true;
}

void Storage::saveState()
{
    // build JSON from current LedController state
    // include led_controller.h above and call its getters directly

    StaticJsonDocument<256> doc;
    doc["mode"] = LedController::getModeStr();
    doc["hz"] = LedController::getBlinkHz();
    doc["period_ms"] = LedController::getBreathePeriod();
    doc["brightness"] = LedController::getBrightness();

    File f = SPIFFS.open("/state.json", FILE_WRITE);
    if (!f)
    {
        Serial.println("Failed to open state.json for writing");
        return;
    }
    serializeJson(doc, f);
    f.close();
    // update cached
    strlcpy(savedMode, doc["mode"] | "breathe", sizeof(savedMode));
    savedBlinkHz = doc["hz"] | savedBlinkHz;
    savedBreathePeriod = doc["period_ms"] | savedBreathePeriod;
    savedBrightness = doc["brightness"] | savedBrightness;

    Serial.println("State saved to /state.json");
}

void Storage::loadState()
{
    if (!SPIFFS.exists("/state.json"))
    {
        Serial.println("/state.json not exists, using defaults");
        return;
    }
    File f = SPIFFS.open("/state.json", FILE_READ);
    if (!f)
    {
        Serial.println("Failed to open /state.json");
        return;
    }
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err)
    {
        Serial.println("Failed parse state.json");
        return;
    }
    const char *mode = doc["mode"] | "breathe";
    strlcpy(savedMode, mode, sizeof(savedMode));
    savedBlinkHz = doc["hz"] | savedBlinkHz;
    savedBreathePeriod = doc["period_ms"] | savedBreathePeriod;
    savedBrightness = doc["brightness"] | savedBrightness;
    Serial.println("Loaded state.json");
}

const char *Storage::getSavedMode() { return savedMode; }
int Storage::getSavedBlinkHz() { return savedBlinkHz; }
int Storage::getSavedBreathePeriod() { return savedBreathePeriod; }
uint8_t Storage::getSavedBrightness() { return savedBrightness; }
