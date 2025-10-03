# Stupid_LED

一个基于 ESP32 的简单 LED 控制固件与内置 Web UI。通过 SoftAP 提供网页控制界面，并通过 WebSocket 接收/下发控制命令。适合快速演示 LED 模式（on/off/blink/breathe）与远程调试。

## 功能

- LED 模式：on / off / blink / breathe
- 调整亮度（0-255）
- Blink：调整频率（Hz）并需点击 Apply 生效
- Breathe：调整周期（ms）并需点击 Apply 生效
- 内置 Web UI（嵌入在固件中，通过 SoftAP 的 HTTP 提供）
- 使用 WebSocket 实现状态广播与命令控制
- 状态上报会包含 uptime、mode、hz、period_ms、brightness、wifi_clients、ws_clients、rssi 等字段

## 文件结构（主要）

- `platformio.ini` - PlatformIO 项目配置
- `src/` - 源码
  - `main.cpp` - 程序入口（初始化模块、主循环）
  - `network.cpp/.h` - 启动 SoftAP、HTTP server 与 WebSocket server；嵌入网页 HTML/JS
  - `websocket_handler.cpp/.h` - WebSocket 消息解析、命令处理、广播接口
  - `status_reporter.cpp/.h` - 汇总设备状态并广播/单发给客户端
  - `led_controller.cpp/.h` - LED 模式逻辑和 PWM 驱动（LEDC）
  - `storage.cpp/.h` - 保存/恢复模式与参数
- `test/` - 单元测试占位（PlatformIO 测试）

## 构建与刷写

本项目使用 PlatformIO 构建（推荐在 VSCode + PlatformIO 插件中使用）。

在项目根目录：

```powershell
# 构建
platformio run
# 上传（示例，确保设备连接并选择正确的上传端口）
platformio run --target upload
```

你也可以在 IDE 中直接使用“Build”与“Upload”按钮。

## 使用说明（网页 UI）

刷写后，ESP32 在 SoftAP 模式下启动一个 WiFi 网络。连接到该网络后，在浏览器打开 http://{AP_IP}/（默认为 192.168.4.1 或在串口启动信息中查看 AP IP）。

网页包含：

- Mode 选择按钮：On / Off / Blink / Breathe
- 亮度滑块（0-255）
- Blink 控件：频率 slider + number + Apply（仅在 mode=blink 时可用）
- Breathe 控件：周期 slider + number + Apply（仅在 mode=breathe 时可用）
- Status 区：显示通过 WebSocket 返回的状态 JSON

前端要点：

- 亮度滑动停止后 200ms 自动应用（不需点击 Apply）
- Blink/Breathe 的调整为滑动更改本地 UI，需点击对应的 Apply 按钮才会把更改发送到设备；若未点击 Apply，则在 5 秒后回退到设备当前值

## WebSocket 协议

WebSocket server 默认监听 81 端口。客户端可以发送 JSON 命令并接收状态或错误事件。

示例：设置模式为 blink（Hz=3）

```json
{ "cmd": "set_mode", "mode": "blink", "hz": 3 }
```

设置亮度：

```json
{ "cmd": "set_brightness", "duty": 200 }
```

请求当前状态：

```json
{ "cmd": "get_status" }
```

设备会广播 `status` 事件（或在单个客户端请求时发送给单个客户端）。示例状态 JSON 字段说明：

- `evt`: 事件类型（例如 `status`）
- `uptime`: 设备已运行的秒数
- `mode`: 当前模式（`on`/`off`/`blink`/`breathe`）
- `hz`: 当前 blink 频率（Hz）
- `period_ms`: 当前 breathe 周期（ms）
- `brightness`: 当前 PWM 占空比 0-255
- `wifi_clients`: SoftAP 上的 WiFi 终端数量（station 数）
- `ws_clients`: 当前 WebSocket 已连接客户端数量
- `rssi`: 信号强度（dBm）：
  - 若有 SoftAP 客户端，固件会尝试使用 ESP-IDF API 获取已连接客户端的 RSSI（返回连接客户端中信号最强的一个的 RSSI）
  - 否则如果设备作为 STA 连接到外部 AP，会返回 `WiFi.RSSI()` 的值
  - 如果两者都不可用，返回 0

## 注意与限制

- RSSI 的获取：SoftAP 模式下通过 `esp_wifi_ap_get_sta_list()` 获取的 RSSI 是设备接收最后数据包时测得的一个瞬时值，可能会跳变；如果没有数据包或客户端刚连接，RSSI 可能未能立即更新。
- JSON 库（ArduinoJson）：部分代码仍使用 `StaticJsonDocument` 等标记为已弃用的 API。编译时会看到警告，建议在未来逐步迁移到新的 API（`JsonDocument` / `doc["key"].is<T>()` 等）。
- WebSocket 与 SoftAP 的 client 计数是分开的：`wifi_clients` 指连接到 SoftAP 的 WiFi 终端数，`ws_clients` 指当前 WebSocket 连接数（可能小于等于 wifi_clients，取决于客户端是否建立 WebSocket）。

## 调试建议

- 使用串口监视器查看日志（Serial.println 输出）以诊断连接状态、WebSocket 事件与上传的 IP 地址。
- 若状态字段异常：在串口看是否有 `WS client connected`/`disconnected` 或 WiFi station 变更的日志。有助于判断是否为网络延迟/缓存问题。

## 贡献

欢迎改进，例如：

- 将前端代码拆到独立文件并通过 SPIFFS 提供（现在是嵌入在固件内的字符串）
- 更细粒度的 RSSI/终端列表上报（按客户端返回 MAC + RSSI）
- 替换 ArduinoJson 的已弃用 API
