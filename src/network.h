#pragma once
#include <WebSocketsServer.h>

namespace Network
{
    void begin(const char *ssid, const char *password);
    void loop();
    WebSocketsServer *getWebSocketServer();
    IPAddress getAPIP();
    int getClientCount();
}
