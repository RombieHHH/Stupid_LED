#include "network.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include "led_controller.h"

static WebServer httpServer(80);
static WebSocketsServer *wsServer = nullptr;
// track previous station count to detect connect/disconnect events
static int prevStations = -1;

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
<html lang="en">
<head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title>ESP32 LED Control</title>
    <style>
        :root{--bg:#0f1720;--card:#0b1220;--accent:#3b82f6;--muted:#94a3b8;--glass:rgba(255,255,255,0.04)}
        html,body{height:100%;margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,'Helvetica Neue',Arial;color:#e6eef8;background:linear-gradient(180deg,#061226 0%, #071730 100%);}
        .wrap{max-width:820px;margin:40px auto;padding:20px}
        .card{background:linear-gradient(180deg,var(--glass),rgba(255,255,255,0.02));backdrop-filter:blur(6px);border-radius:12px;padding:18px;box-shadow:0 6px 24px rgba(2,6,23,0.6)}
        h1{margin:0 0 8px;font-size:20px}
        .row{display:flex;gap:12px;align-items:center;margin-top:12px}
        .col{flex:1}
        label{display:block;font-size:13px;color:var(--muted);margin-bottom:6px}
        input[type=range]{width:100%}
        input[type=number]{width:100%;padding:8px;border-radius:6px;border:1px solid rgba(255,255,255,0.06);background:transparent;color:inherit}
        .btn{background:var(--accent);color:white;padding:8px 12px;border-radius:8px;border:none;cursor:pointer}
        .mode-btn{background:transparent;border:1px solid rgba(255,255,255,0.04);color:var(--muted);padding:8px 10px;border-radius:8px;cursor:pointer}
        .status{font-family:monospace;background:rgba(0,0,0,0.25);padding:10px;border-radius:8px;color:#dbeafe}
        .flex{display:flex;gap:8px}
        .right{text-align:right}
        footer{margin-top:14px;font-size:12px;color:var(--muted);text-align:center}
    </style>
</head>
<body>
    <div class="wrap">
        <div class="card">
            <h1>ESP32 LED Control</h1>
            <div class="row">
                <div class="col">
                    <label>Mode</label>
                    <div class="flex">
                        <button class="mode-btn" onclick="setMode('on')">On</button>
                        <button class="mode-btn" onclick="setMode('off')">Off</button>
                        <button class="mode-btn" onclick="setMode('blink')">Blink</button>
                        <button class="mode-btn" onclick="setMode('breathe')">Breathe</button>
                    </div>
                </div>
                <div class="col right">
                    <label>Connection</label>
                    <div id="wsstate" class="status">disconnected</div>
                </div>
            </div>

            <div class="row">
                <div class="col">
                    <label>Brightness <span id="bval">128</span></label>
                    <input id="b" type="range" min="0" max="255" value="128" oninput="onBrightness(this.value)" />
                </div>
                <div style="width:180px">
                    <label>Blink Hz</label>
                    <input id="hz" type="range" min="1" max="20" value="2" oninput="onHz(this.value)" />
                    <input id="hznum" type="number" min="1" max="20" value="2" oninput="onHz(this.value)" />
                </div>
            </div>

            <div class="row">
                <div class="col">
                    <label>Breathe period (ms) <span id="periodval">1500</span></label>
                    <input id="period" type="range" min="200" max="5000" step="50" value="1500" oninput="onPeriod(this.value)" />
                </div>
                <div style="width:180px">
                    <label>Apply</label>
                    <div class="flex">
                        <button class="btn" onclick="applyBlink()">Apply Blink</button>
                        <button class="btn" onclick="applyBreathe()">Apply Breathe</button>
                    </div>
                </div>
            </div>

            <div style="margin-top:14px">
                <label>Status</label>
                <div id="status" class="status">no data</div>
            </div>

            <footer>Use controls or WebSocket to control the LED. Page reconnects automatically.</footer>
        </div>
    </div>

    <script>
        let ws;
        function connect(){
            const url = 'ws://' + location.hostname + ':81/';
            ws = new WebSocket(url);
            ws.addEventListener('open', ()=>{ document.getElementById('wsstate').innerText = 'connected'; });
            ws.addEventListener('close', ()=>{ document.getElementById('wsstate').innerText = 'disconnected'; setTimeout(connect,1000); });
            ws.addEventListener('message', (evt)=>{
                try{
                    const obj = JSON.parse(evt.data);
                    document.getElementById('status').innerText = JSON.stringify(obj, null, 2);
                    // update UI from status
                    if(obj.mode) document.querySelectorAll('.mode-btn').forEach(b=>{ b.style.borderColor = 'rgba(255,255,255,0.04)'; });
                    if(obj.brightness!==undefined){ document.getElementById('b').value = obj.brightness; document.getElementById('bval').innerText = obj.brightness; }
                    if(obj.hz!==undefined){ document.getElementById('hz').value = obj.hz; document.getElementById('hznum').value = obj.hz; }
                    if(obj.period_ms!==undefined){ document.getElementById('period').value = obj.period_ms; document.getElementById('periodval').innerText = obj.period_ms; }
                }catch(e){ console.log('invalid json', e); }
            });
        }

        function send(obj){ if(ws && ws.readyState===1) ws.send(JSON.stringify(obj)); }
        function setMode(m){ send({cmd:'set_mode', mode:m}); }
        function onBrightness(v){ document.getElementById('bval').innerText = v; }
        function onHz(v){ document.getElementById('hz').value = v; document.getElementById('hznum').value = v; }
        function onPeriod(v){ document.getElementById('periodval').innerText = v; }
        function applyBlink(){ const hz = parseInt(document.getElementById('hz').value||2); send({cmd:'set_mode', mode:'blink', hz:hz}); }
        function applyBreathe(){ const p = parseInt(document.getElementById('period').value||1500); send({cmd:'set_mode', mode:'breathe', period_ms:p}); }
        function applyBrightness(){ const d = parseInt(document.getElementById('b').value||128); send({cmd:'set_brightness', duty:d}); }
        // send brightness on change end (debounce)
        let bTimeout;
        document.addEventListener('input', (e)=>{ if(e.target && e.target.id==='b'){ clearTimeout(bTimeout); bTimeout=setTimeout(()=>{ applyBrightness(); }, 200); } });

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

    // initialize prevStations so we don't print a connect/disconnect on boot
    prevStations = WiFi.softAPgetStationNum();
}

void Network::loop()
{
    httpServer.handleClient();
    if (wsServer)
        wsServer->loop();

    // monitor WiFi station count and notify on changes
    int stations = WiFi.softAPgetStationNum();
    if (stations != prevStations)
    {
        if (stations == 0)
        {
            Serial.println("WiFi: no stations connected");
            // no wifi stations -> enter breathe waiting mode
            LedController::enterBreatheWait();
        }
        else
        {
            Serial.printf("WiFi: stations connected=%d\n", stations);
            // when user connects via WiFi, cancel breathe wait
            LedController::onClientConnected();
        }
        prevStations = stations;
    }
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
