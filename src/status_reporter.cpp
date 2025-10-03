#include "status_reporter.h"
#include "led_controller.h"
#include "network.h"
#include "websocket_handler.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <limits.h>

static unsigned long startMillis = 0;

namespace StatusReporter
{
    void begin()
    {
        // 记录启动时间用于 uptime 计算
        startMillis = millis();
    }

    void broadcast()
    {
        StaticJsonDocument<256> doc;
        doc["evt"] = "status";
        doc["uptime"] = (unsigned long)((millis() - startMillis) / 1000);
    // 计算 RSSI：
    // - 如果有 SoftAP 客户端，尝试通过 esp_wifi_ap_get_sta_list() 获取客户端的 RSSI
    // - 否则如果作为 STA 连接，则使用 WiFi.RSSI()
        int rssiVal = 0;
        if (WiFi.softAPgetStationNum() > 0)
        {
            wifi_sta_list_t sta_list;
            if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0)
            {
                int best = INT_MIN;
                for (int i = 0; i < (int)sta_list.num; ++i)
                {
                    wifi_sta_info_t info = sta_list.sta[i];
                    if ((int)info.rssi > best)
                        best = (int)info.rssi;
                }
                if (best != INT_MIN)
                    rssiVal = best;
            }
        }
        else if (WiFi.status() == WL_CONNECTED)
        {
            rssiVal = WiFi.RSSI();
        }
        doc["rssi"] = rssiVal;
        doc["mode"] = LedController::getModeStr();
        doc["hz"] = LedController::getBlinkHz();
        doc["period_ms"] = LedController::getBreathePeriod();
    doc["brightness"] = LedController::getBrightness();
    doc["dropped"] = WebsocketHandler::getDropped();
        doc["wifi_clients"] = WiFi.softAPgetStationNum();
        doc["ws_clients"] = WebsocketHandler::getConnectedCount();

        String out;
        serializeJson(doc, out);
        WebsocketHandler::broadcastText(out);
    }

    void sendTo(int clientNum)
    {
        StaticJsonDocument<256> doc;
        doc["evt"] = "status";
        doc["uptime"] = (unsigned long)((millis() - startMillis) / 1000);
        int rssiVal = 0;
        if (WiFi.softAPgetStationNum() > 0)
        {
            wifi_sta_list_t sta_list;
            if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0)
            {
                int best = INT_MIN;
                for (int i = 0; i < (int)sta_list.num; ++i)
                {
                    wifi_sta_info_t info = sta_list.sta[i];
                    if ((int)info.rssi > best)
                        best = (int)info.rssi;
                }
                if (best != INT_MIN)
                    rssiVal = best;
            }
        }
        else if (WiFi.status() == WL_CONNECTED)
        {
            rssiVal = WiFi.RSSI();
        }
        doc["rssi"] = rssiVal;
        doc["mode"] = LedController::getModeStr();
        doc["hz"] = LedController::getBlinkHz();
        doc["period_ms"] = LedController::getBreathePeriod();
    doc["brightness"] = LedController::getBrightness();
    doc["dropped"] = WebsocketHandler::getDropped();
    doc["wifi_clients"] = WiFi.softAPgetStationNum();
        doc["ws_clients"] = WebsocketHandler::getConnectedCount();

        String out;
        serializeJson(doc, out);
        // 发送到指定客户端
        auto ws = Network::getWebSocketServer();
        if (ws)
            ws->sendTXT((uint8_t)clientNum, out);
    }

    void sendTo(uint8_t clientNum)
    {
        sendTo((int)clientNum);
    }
}
