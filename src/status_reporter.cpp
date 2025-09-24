#include "status_reporter.h"
#include "led_controller.h"
#include "network.h"
#include "websocket_handler.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static unsigned long startMillis = 0;

namespace StatusReporter
{
    void begin()
    {
        startMillis = millis();
    }

    void broadcast()
    {
        StaticJsonDocument<256> doc;
        doc["evt"] = "status";
        doc["uptime"] = (unsigned long)((millis() - startMillis) / 1000);
        doc["rssi"] = WiFi.softAPgetStationNum() ? WiFi.RSSI() : 0; // approximate
        doc["mode"] = LedController::getModeStr();
        doc["hz"] = LedController::getBlinkHz();
        doc["period_ms"] = LedController::getBreathePeriod();
        doc["brightness"] = LedController::getBrightness();
        // simple qlen/dropped placeholders
        doc["qlen_rx"] = 0;
        doc["qlen_tx"] = 0;
        doc["dropped"] = 0;

        String out;
        serializeJson(doc, out);
        WebsocketHandler::broadcastText(out);
    }

    void sendTo(int clientNum)
    {
        StaticJsonDocument<256> doc;
        doc["evt"] = "status";
        doc["uptime"] = (unsigned long)((millis() - startMillis) / 1000);
        doc["rssi"] = WiFi.softAPgetStationNum() ? WiFi.RSSI() : 0;
        doc["mode"] = LedController::getModeStr();
        doc["hz"] = LedController::getBlinkHz();
        doc["period_ms"] = LedController::getBreathePeriod();
        doc["brightness"] = LedController::getBrightness();
        doc["qlen_rx"] = 0;
        doc["qlen_tx"] = 0;
        doc["dropped"] = 0;

        String out;
        serializeJson(doc, out);
        // send to a single client through ws server: use Network::getWebSocketServer()
        auto ws = Network::getWebSocketServer();
        if (ws)
            ws->sendTXT((uint8_t)clientNum, out);
    }

    void sendTo(uint8_t clientNum)
    {
        // forward to int-based implementation
        sendTo((int)clientNum);
    }
}
