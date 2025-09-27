#include <Arduino.h>
#include "network.h"
#include "led_controller.h"
#include "storage.h"
#include "websocket_handler.h"
#include "status_reporter.h"

// Config
#define AP_SSID "ESP32C3_LED_AP"
#define AP_PSK "12345678"  

unsigned long lastStatusMillis = 0;

void setup()
{
  Serial.begin(115200);
  delay(200);

  // 初始化 SPIFFS（用于持久化 state 并提供网页）
  if (!Storage::begin())
  {
    Serial.println("SPIFFS init failed!");
  }
  else
  {
    Serial.println("SPIFFS ready");
  }

  // 加载保存的状态（如果有）
  Storage::loadState();

  // 初始化 LED 控制器（从 Storage 读取初始状态并生效）
  LedController::begin();

  // 初始化网络（SoftAP、HTTP 与 WebSocket 服务器）
  Network::begin(AP_SSID, AP_PSK);

  // 初始化 websocket handler（使用 Network 提供的 wsServer）
  WebsocketHandler::begin(Network::getWebSocketServer());

  // 初始化状态上报模块
  StatusReporter::begin();

  Serial.println("Setup complete");
}

void loop()
{
  // 轮询网络与 WebSocket
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

  delay(1); // 让出少量 CPU 时间
}
