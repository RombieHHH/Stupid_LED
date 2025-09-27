#include "websocket_handler.h"
#include "led_controller.h"
#include "storage.h"
#include "status_reporter.h"
#include "network.h"
#include <ArduinoJson.h>

static WebSocketsServer *ws = nullptr;
static int connectedClients = 0;
static unsigned long lastMsgMillis = 0;
static uint32_t dropped = 0; // 用于记录在无客户端时被丢弃的广播计数（背压统计）

// 向单个客户端发送错误事件
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
        // 客户端连接：取消 breathe-wait，并立即发送状态给该客户端
        LedController::onClientConnected();
        StatusReporter::sendTo(num);
        return;
    }
    else if (type == WStype_DISCONNECTED)
    {
        connectedClients = max(0, connectedClients - 1);
        Serial.printf("Websocket disconnected clients=%d\n", connectedClients);
        // 仅当 SoftAP 上没有 station（WiFi 客户端）时才进入 breathe-wait。
        // 这样可以避免单纯 WS 断开（例如短暂网络抖动）导致不必要的模式切换。
        int stations = Network::getClientCount();
        Serial.printf("WiFi stations=%d\n", stations);
        if (stations == 0)
        {
            LedController::enterBreatheWait();
        }
        return;
    }
    else if (type == WStype_TEXT)
    {
        lastMsgMillis = millis();
        // 处理文本消息，期望是 JSON 格式
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
                    // 对于 on/off 模式，不允许携带额外字段如 hz/period_ms
                    if (doc.containsKey("hz") || doc.containsKey("period_ms") || doc.containsKey("duty"))
                    {
                        sendError(num, "bad_request", "unknown field");
                        return;
                    }
                    LedController::setModeOn();
                    Storage::saveState();
                }
                else if (strcmp(mode, "off") == 0)
                {
                    if (doc.containsKey("hz") || doc.containsKey("period_ms") || doc.containsKey("duty"))
                    {
                        sendError(num, "bad_request", "unknown field");
                        return;
                    }
                    LedController::setModeOff();
                    Storage::saveState();
                }
                else if (strcmp(mode, "blink") == 0)
                {
                    // blink expects 'hz' only
                    if (doc.containsKey("period_ms"))
                    {
                        sendError(num, "bad_request", "unknown field hz");
                        return;
                    }
                    int hz = doc.containsKey("hz") ? doc["hz"].as<int>() : 2;
                    LedController::setModeBlink(max(1, hz));
                    Storage::saveState();
                }
                else if (strcmp(mode, "breathe") == 0)
                {
                    // breathe expects 'period_ms' only
                    if (doc.containsKey("hz"))
                    {
                        sendError(num, "bad_request", "unknown field hz");
                        return;
                    }
                    int period = doc.containsKey("period_ms") ? doc["period_ms"].as<int>() : 1500;
                    LedController::setModeBreathe(max(200, period));
                    Storage::saveState();
                }
                else
                {
                    sendError(num, "bad_request", "unknown mode");
                    return;
                }
                // 操作成功：广播最新状态用于 UI 更新
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
                // 不允许携带多余字段
                if (doc.containsKey("hz") || doc.containsKey("period_ms"))
                {
                    sendError(num, "bad_request", "unknown field hz");
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
    // 覆写 onEvent 指向处理函数
    ws->onEvent([](uint8_t num, WStype_t type, uint8_t *payload, size_t length)
                { handleWSMessage(num, type, payload, length); });
}

void WebsocketHandler::loop()
{
    // 不进行操作：ws->loop() 在 Network::loop() 中调用
}

void WebsocketHandler::broadcastText(const String &s)
{
    if (!ws)
        return;
    if (ws->connectedClients() == 0)
    {
        // 没有客户端连接时，丢弃消息并计数
        dropped++;
        return;
    }
    // 客户端恢复连接，且之前有丢弃的消息，发送警告
    if (dropped > 0)
    {
        StaticJsonDocument<128> alert;
        alert["evt"] = "alert";
        alert["type"] = "backpressure";
        alert["dropped"] = dropped;
        String aout;
        serializeJson(alert, aout);
        ws->broadcastTXT(aout);
        // reset dropped after notifying
        dropped = 0;
    }
    String tmp = s;
    ws->broadcastTXT(tmp);
}

int WebsocketHandler::getConnectedCount()
{
    return connectedClients;
}

int WebsocketHandler::getDropped()
{
    return (int)dropped;
}
