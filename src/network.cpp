#include "network.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include "led_controller.h"

static WebServer httpServer(80);
static WebSocketsServer *wsServer = nullptr;
// 跟踪上一次的 station 数，用于检测 WiFi 客户端连接/断开事件
static int prevStations = -1;

static void handleRoot()
{
    httpServer.send(200, "text/html", R"rawliteral(
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
        .hidden{display:none}
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
            </div>

            <!-- Blink 控件 -->
            <div id="blinkControls" class="row hidden">
                <div style="flex:1">
                    <label>Blink Hz</label>
                    <input id="hz" type="range" min="1" max="20" value="2" oninput="onHz(this.value)" />
                    <input id="hznum" type="number" min="1" max="20" value="2" oninput="onHz(this.value)" />
                </div>
                <div style="width:180px">
                    <label>Apply</label>
                    <button class="btn" onclick="applyBlink()">Apply Blink</button>
                </div>
            </div>

            <!-- Breathe 控件 -->
            <div id="breatheControls" class="row hidden">
                <div style="flex:1">
                    <label>Breathe period (ms)</label>
                    <input id="period" type="range" min="200" max="5000" step="50" value="1500" oninput="onPeriod(this.value)" />
                    <input id="periodnum" type="number" min="200" max="5000" step="50" value="1500" oninput="onPeriod(this.value)" />
                </div>
                <div style="width:180px">
                    <label>Apply</label>
                    <button class="btn" onclick="applyBreathe()">Apply Breathe</button>
                </div>
            </div>

            <div style="margin-top:14px">
                <label>Message</label>
                <div id="message" class="status">no data</div>
            </div>

            <footer>Use controls or WebSocket to control the LED. Page reconnects automatically.</footer>
        </div>
    </div>

    <script>
        let ws;
        // server-known values (keep in sync with server broadcasts)
        let serverHz = 2;
        let serverPeriod = 1500;
        // editing state / timers for revert behavior
        let editingHz = false, editingPeriod = false;
        let hzTimer = null, periodTimer = null;

        function connect(){
            const url = 'ws://' + location.hostname + ':81/';
            ws = new WebSocket(url);
            ws.addEventListener('open', ()=>{ document.getElementById('wsstate').innerText = 'connected'; });
            ws.addEventListener('close', ()=>{ document.getElementById('wsstate').innerText = 'disconnected'; setTimeout(connect,1000); });
            ws.addEventListener('message', (evt)=>{
                try{
                    const obj = JSON.parse(evt.data);
                    // Show latest full message JSON in the Message box (including dropped)
                    document.getElementById('message').innerText = JSON.stringify(obj, null, 2);
                    // keep UI controls in sync when status-like messages arrive
                    if(obj.mode) updateModeUI(obj.mode);
                    if(typeof obj.hz !== 'undefined'){
                        serverHz = obj.hz;
                        if(!editingHz){ document.getElementById('hz').value = serverHz; document.getElementById('hznum').value = serverHz; }
                    }
                    if(typeof obj.period_ms !== 'undefined'){
                        serverPeriod = obj.period_ms;
                        if(!editingPeriod){ document.getElementById('period').value = serverPeriod; document.getElementById('periodnum').value = serverPeriod; }
                    }
                }catch(e){ console.log('invalid json', e); }
            });
        }

        function send(obj){ if(ws && ws.readyState===1) ws.send(JSON.stringify(obj)); }

        function setMode(m){ 
            send({cmd:'set_mode', mode:m}); 
            updateModeUI(m);
        }

        function updateModeUI(mode){
            document.querySelectorAll('.mode-btn').forEach(b=>b.style.borderColor='rgba(255,255,255,0.04)');
            document.getElementById('blinkControls').classList.add('hidden');
            document.getElementById('breatheControls').classList.add('hidden');
            // also disable inputs when not active to prevent accidental edits
            const hzEl = document.getElementById('hz');
            const hzNum = document.getElementById('hznum');
            const pEl = document.getElementById('period');
            const pNum = document.getElementById('periodnum');
            if(mode==='blink'){
                document.getElementById('blinkControls').classList.remove('hidden');
                hzEl.disabled = false; hzNum.disabled = false;
            } else {
                hzEl.disabled = true; hzNum.disabled = true;
            }
            if(mode==='breathe'){
                document.getElementById('breatheControls').classList.remove('hidden');
                pEl.disabled = false; pNum.disabled = false;
            } else {
                pEl.disabled = true; pNum.disabled = true;
            }
        }

        function onBrightness(v){ document.getElementById('bval').innerText = v; }

        function onHz(v){ 
            // user is editing the hz; start/refresh a 5s revert timer
            editingHz = true;
            clearTimeout(hzTimer);
            document.getElementById('hz').value = v; 
            document.getElementById('hznum').value = v; 
            hzTimer = setTimeout(()=>{
                // revert to last server value if apply wasn't clicked
                editingHz = false;
                document.getElementById('hz').value = serverHz;
                document.getElementById('hznum').value = serverHz;
            }, 5000);
        }

        function onPeriod(v){ 
            // user is editing the period; start/refresh a 5s revert timer
            editingPeriod = true;
            clearTimeout(periodTimer);
            document.getElementById('period').value = v; 
            document.getElementById('periodnum').value = v; 
            periodTimer = setTimeout(()=>{
                // revert to last server value if apply wasn't clicked
                editingPeriod = false;
                document.getElementById('period').value = serverPeriod;
                document.getElementById('periodnum').value = serverPeriod;
            }, 5000);
        }

        function applyBlink(){ 
            const hz = parseInt(document.getElementById('hz').value||2);
            // clear revert timer and mark editing done
            clearTimeout(hzTimer); editingHz = false; serverHz = hz;
            send({cmd:'set_mode', mode:'blink', hz:hz}); 
        }
        function applyBreathe(){ 
            const p = parseInt(document.getElementById('period').value||1500);
            // clear revert timer and mark editing done
            clearTimeout(periodTimer); editingPeriod = false; serverPeriod = p;
            send({cmd:'set_mode', mode:'breathe', period_ms:p}); 
        }
        function applyBrightness(){ 
            const d = parseInt(document.getElementById('b').value||128); 
            send({cmd:'set_brightness', duty:d}); 
        }

        let bTimeout;
        document.addEventListener('input', (e)=>{ 
            if(e.target && e.target.id==='b'){ 
                clearTimeout(bTimeout); 
                bTimeout=setTimeout(()=>{ applyBrightness(); }, 200); 
            } 
        });

        connect();
    </script>
</body>
</html>
)rawliteral");
}

void Network::begin(const char *ssid, const char *password)
{
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
    Serial.printf("SSID: %s  Password: %s\n", ssid, password ? password : "(none)");
    Serial.print("AP IP: ");
    Serial.println(apIP);

    // 启动 HTTP 服务器
    httpServer.on("/", handleRoot);
    httpServer.begin();
    Serial.println("HTTP server started");

    // 在81端口启动 WebSocket 服务器
    wsServer = new WebSocketsServer(81);
    wsServer->begin();
    wsServer->onEvent([](uint8_t num, WStype_t type, uint8_t *payload, size_t length)
                      {
    // 将事件转发给 WebsocketHandler（websocket 模块会获取 wsServer 的指针）
    // 如果 websocket_handler 未链接到这里，websocket_handler 会在 begin 时自行注册回调。

    if(type == WStype_CONNECTED){
      Serial.printf("WS client #%d connected\n", num);
    } else if(type == WStype_DISCONNECTED){
      Serial.printf("WS client #%d disconnected\n", num);
    } });

    Serial.println("WebSocket server started on port 81");

    // 初始化 prevStations，避免启动时打印连接/断开信息
    prevStations = WiFi.softAPgetStationNum();
}

void Network::loop()
{
    httpServer.handleClient();
    if (wsServer)
        wsServer->loop();

    // 检测wifi连接数量并更新新状态
    // 检测 WiFi station 连接/断开事件
    int stations = WiFi.softAPgetStationNum();
    if (stations != prevStations)
    {
        if (stations == 0)
        {
            Serial.println("WiFi: no stations connected");
            // wifi连接断开，进入呼吸模式
            LedController::enterBreatheWait();
        }
        else
        {
            Serial.printf("WiFi: stations connected=%d\n", stations);
            // 有新的wifi连接，退出呼吸模式
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
    return WiFi.softAPgetStationNum();
}
