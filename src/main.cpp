#include <Arduino.h>
#include "network.h"
#include "led_controller.h"
#include "storage.h"
#include "websocket_handler.h"
#include "status_reporter.h"

// Config
#define AP_SSID "ESP32C3_LED_AP"
#define AP_PSK "12345678" // 推荐至少8位，若想开放则留空

unsigned long lastStatusMillis = 0;

void setup()
{
  Serial.begin(115200);
  delay(200);

  // 初始化 SPIFFS（用于保存 state + 提供网页）
  if (!Storage::begin())
  {
    Serial.println("SPIFFS init failed!");
  }
  else
  {
    Serial.println("SPIFFS ready");
  }

  // 加载保存的状态（若有）
  Storage::loadState();

  // 初始化 LED 控制器（会读取 Storage 中的初始 state）
  LedController::begin();

  // 初始化网络（SoftAP + HTTP + WebSocket server）
  Network::begin(AP_SSID, AP_PSK);

  // 初始化 websocket handler（需要 network 提供 wsServer 引用）
  WebsocketHandler::begin(Network::getWebSocketServer());

  // 初始化 status 报告者
  StatusReporter::begin();

  Serial.println("Setup complete");
}

void loop()
{
  // WebSocket library 的 loop 调用
  Network::loop();

  // 轮询 LED 控制器（用于 breathe/flash 时间步进）
  LedController::update();

  // 轮询 websocket handler（处理缓存/重发等）
  WebsocketHandler::loop();

  // 定期广播状态（例如每 2000ms）
  unsigned long now = millis();
  if (now - lastStatusMillis > 2000)
  {
    lastStatusMillis = now;
    StatusReporter::broadcast();
  }

  delay(1); // 稍微让出 CPU
}
