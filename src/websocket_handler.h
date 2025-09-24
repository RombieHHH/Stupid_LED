#pragma once
#include <WebSocketsServer.h>

namespace WebsocketHandler
{
    void begin(WebSocketsServer *server);
    void loop();
    void broadcastText(const String &s);
}
