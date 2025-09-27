#include "websocket_handler.h"
#include "led_controller.h"
#include "storage.h"
#include "status_reporter.h"
#include "network.h"
#include <ArduinoJson.h>

static WebSocketsServer *ws = nullptr;
static int connectedClients = 0;
static unsigned long lastMsgMillis = 0;
static uint32_t dropped = 0; // simple dropped counter for backpressure

void sendError(uint8_t num, const char *code, const char *msg)
{
    StaticJsonDocument<256> doc;
    doc["evt"] = "error";
    doc["code"] = code;
    doc["msg"] = msg;
    String out;
    serializeJson(doc, out);
    if (ws)
        ws->sendTXT(num, out);
}

void handleWSMessage(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    if (type == WStype_CONNECTED)
    {
        connectedClients++;
        Serial.printf("Websocket connected clients=%d\n", connectedClients);
        // cancel breathe wait if necessary
        LedController::onClientConnected();
        // send immediate status
        StatusReporter::sendTo(num);
        return;
    }
    else if (type == WStype_DISCONNECTED)
    {
        connectedClients = max(0, connectedClients - 1);
        Serial.printf("Websocket disconnected clients=%d\n", connectedClients);
        // Only enter breathe-wait when there are no WiFi stations connected.
        // WebSocket disconnect alone (e.g. temporary network blip) should not
        // force the LED into breathe mode if the client still has WiFi association.
        int stations = Network::getClientCount();
        Serial.printf("WiFi stations=%d\n", stations);
        if (stations == 0)
        {
            // no wifi stations -> enter breathe waiting mode
            LedController::enterBreatheWait();
        }
        return;
    }
    else if (type == WStype_TEXT)
    {
        lastMsgMillis = millis();
        // parse JSON
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err)
        {
            sendError(num, "bad_request", "invalid json");
            return;
        }
        if (doc.containsKey("cmd"))
        {
            const char *cmd = doc["cmd"];
            if (strcmp(cmd, "set_mode") == 0)
            {
                if (!doc.containsKey("mode"))
                {
                    sendError(num, "bad_request", "missing mode");
                    return;
                }
                const char *mode = doc["mode"];
                if (strcmp(mode, "on") == 0)
                {
                    LedController::setModeOn();
                    Storage::saveState();
                }
                else if (strcmp(mode, "off") == 0)
                {
                    LedController::setModeOff();
                    Storage::saveState();
                }
                else if (strcmp(mode, "blink") == 0)
                {
                    int hz = doc.containsKey("hz") ? doc["hz"].as<int>() : 2;
                    LedController::setModeBlink(max(1, hz));
                    Storage::saveState();
                }
                else if (strcmp(mode, "breathe") == 0)
                {
                    int period = doc.containsKey("period_ms") ? doc["period_ms"].as<int>() : 1500;
                    LedController::setModeBreathe(max(200, period));
                    Storage::saveState();
                }
                else
                {
                    sendError(num, "bad_request", "unknown mode");
                    return;
                }
                // ack: send status
                StatusReporter::broadcast();
                return;
            }
            else if (strcmp(cmd, "set_brightness") == 0)
            {
                if (!doc.containsKey("duty"))
                {
                    sendError(num, "bad_request", "missing duty");
                    return;
                }
                int duty = doc["duty"].as<int>();
                duty = constrain(duty, 0, 255);
                LedController::setBrightness(duty);
                Storage::saveState();
                StatusReporter::broadcast();
                return;
            }
            else if (strcmp(cmd, "get_status") == 0)
            {
                StatusReporter::sendTo(num);
                return;
            }
            else
            {
                sendError(num, "bad_request", "unknown cmd");
                return;
            }
        }
        else
        {
            sendError(num, "bad_request", "missing cmd");
            return;
        }
    }
}

void WebsocketHandler::begin(WebSocketsServer *server)
{
    ws = server;
    if (!ws)
        return;
    // override the onEvent - point to our handler function
    ws->onEvent([](uint8_t num, WStype_t type, uint8_t *payload, size_t length)
                { handleWSMessage(num, type, payload, length); });
}

void WebsocketHandler::loop()
{
    // no-op: ws->loop happens in Network::loop()
}

void WebsocketHandler::broadcastText(const String &s)
{
    if (!ws)
        return;
    if (ws->connectedClients() == 0)
    {
        // no clients, increment dropped
        dropped++;
        return;
    }
    // WebSocketsServer::broadcastTXT takes a non-const String& on some cores;
    // pass a local copy to avoid qualifier/const issues with some compilers.
    String tmp = s;
    ws->broadcastTXT(tmp);
}

int WebsocketHandler::getConnectedCount()
{
    return connectedClients;
}
