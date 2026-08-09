# ESP32 联网协处理器

该 ESP-IDF 工程负责 STM32 串口协议、Wi-Fi Station、MQTT 客户端和 JSON 上报。目标硬件为 ESP32-WROOM-32 DevKit，ESP-IDF 5.2.7。

## UART 接线

```text
STM32 PA2 (USART2 TX) -> ESP32 GPIO16 (UART2 RX)
STM32 PA3 (USART2 RX) <- ESP32 GPIO17 (UART2 TX)
STM32 GND             -> ESP32 GND
```

两块开发板分别用 USB 供电，不连接两板的 3.3V/5V。

## 网络配置

首次使用时在 VS Code 命令面板打开 `ESP-IDF: SDK Configuration editor (menuconfig)`，进入 `Industrial gateway network configuration`，填写：

- Wi-Fi SSID 与密码；
- MQTT Broker URI，例如 `mqtt://192.168.1.100`；
- 可选的 MQTT 用户名和密码；
- 唯一设备 ID。

未填写 SSID 时，固件保持离线，但 UART 心跳 ACK 和数据解析仍正常工作。

配置完成后，默认使用总命令：

```text
ESP-IDF: Build, Flash and Monitor your Device
```

## MQTT Topics

```text
industrial_gateway/<device-id>/status
industrial_gateway/<device-id>/telemetry
industrial_gateway/<device-id>/modbus
```

`status` 使用 retained 消息，并配置 `offline` Last Will；连接成功时发布 `online`。

## 看门狗与安全省电

- `GatewayUartTask`加入ESP-IDF任务看门狗，循环正常运行时持续复位计时器；
- 任务看门狗超时为10秒并启用panic，任务长期阻塞时由系统复位恢复；
- 启动日志输出ESP32复位原因，便于区分上电、软件复位和看门狗复位；
- Wi-Fi启动后使用`WIFI_PS_MIN_MODEM`，在维持Station及MQTT连接的前提下降低无线空闲功耗；
- 本方案不使用Light-sleep/Deep-sleep，避免UART心跳、MQTT连接和在线状态被睡眠流程破坏。

设计与实测步骤见`../docs/设计与实验流程_v0.7_看门狗与安全浅睡眠.md`。
