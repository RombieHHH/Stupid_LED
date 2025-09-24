#include "network.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>

static WebServer httpServer(80);
static WebSocketsServer *wsServer = nullptr;

static void handleRoot()
{
    // 尝试从 SPIFFS 读取 /index.html
    if (SPIFFS.exists("/index.html"))
    {
        File f = SPIFFS.open("/index.html", "r");
        if (f)
        {
            httpServer.streamFile(f, "text/html");
            f.close();
            return;
        }
    }
    // fallback simple page
    httpServer.send(200, "text/html", "<html><body><h3>ESP32 WebSocket LED</h3></body></html>");
}

// Note: older/newer esp32 Arduino cores expose different WiFi event enums/unions.
// To avoid compilation issues across cores, we don't register a WiFi event
// callback here. Instead we rely on runtime queries (WiFi.softAPgetStationNum())
// via Network::getClientCount().

void Network::begin(const char *ssid, const char *password)
{
    // Don't register a WiFi event handler to keep compatibility with multiple
    // Arduino-ESP32 core versions.
    Serial.println("Starting SoftAP...");
    if (password && strlen(password) >= 8)
    {
        WiFi.softAP(ssid, password);
    }
    else
    {
        WiFi.softAP(ssid);
    }
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("AP IP: ");
    Serial.println(apIP);

    // start HTTP server
    httpServer.on("/", handleRoot);
    httpServer.begin();
    Serial.println("HTTP server started");

    // prepare index.html in SPIFFS if not present (optional simple built-in)
    if (!SPIFFS.exists("/index.html"))
    {
        File f = SPIFFS.open("/index.html", "w");
        if (f)
        {
            f.print(R"rawliteral(
<!doctype html>
<html>
<head><meta charset="utf-8"><title>ESP32 LED Control</title></head>
<body>
<h3>ESP32 LED Control</h3>
<div id="status"></div>
<button onclick="send({'cmd':'set_mode','mode':'on'})">ON</button>
<button onclick="send({'cmd':'set_mode','mode':'off'})">OFF</button>
<button onclick="send({'cmd':'set_mode','mode':'blink','hz':2})">Blink 2Hz</button>
<button onclick="send({'cmd':'set_mode','mode':'breathe','period_ms':1500})">Breathe</button>
<br/>
<label>Brightness<input id="b" type="range" min="0" max="255" value="128" onchange="setB(this.value)"></label>
<script>
var ws;
function connect(){
  ws = new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen = function(){ console.log('ws open'); };
  ws.onmessage = function(evt){
    try{
      var obj = JSON.parse(evt.data);
      document.getElementById('status').innerText = JSON.stringify(obj);
    }catch(e){ console.log(e); }
  };
  ws.onclose = function(){ setTimeout(connect,1000); };
}
function send(obj){ if(ws && ws.readyState===1) ws.send(JSON.stringify(obj)); }
function setB(v){ send({'cmd':'set_brightness','duty':parseInt(v)}); }
connect();
</script>
</body>
</html>
      )rawliteral");
            f.close();
        }
    }

    // start websocket server on port 81
    wsServer = new WebSocketsServer(81);
    wsServer->begin();
    wsServer->onEvent([](uint8_t num, WStype_t type, uint8_t *payload, size_t length)
                      {
    // delegate to WebsocketHandler (we will get a pointer to wsServer in websocket module)
    // but since websocket_handler may not be linked here, we will let websocket_handler attach its callback later.
    // For safety, print basic info
    if(type == WStype_CONNECTED){
      Serial.printf("WS client #%d connected\n", num);
    } else if(type == WStype_DISCONNECTED){
      Serial.printf("WS client #%d disconnected\n", num);
    } });

    Serial.println("WebSocket server started on port 81");
}

void Network::loop()
{
    httpServer.handleClient();
    if (wsServer)
        wsServer->loop();
}

WebSocketsServer *Network::getWebSocketServer()
{
    return wsServer;
}

IPAddress Network::getAPIP()
{
    return WiFi.softAPIP();
}

int Network::getClientCount()
{
    // Query the SoftAP station count directly from the WiFi stack.
    return WiFi.softAPgetStationNum();
}
