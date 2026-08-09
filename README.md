# 基于 FreeRTOS 的多功能工业网关

本项目采用 STM32F103C8T6 + ESP32-WROOM-32 双控制器架构：STM32运行FreeRTOS，负责实时采集、Modbus现场通信、本地状态和故障判断；ESP32负责Wi-Fi、MQTT和云端数据上报；Qt 6/C++上位机负责设备状态、遥测数据、Modbus结果和通信日志的可视化。

已完成实验见 [docs/实验记录索引.md](docs/实验记录索引.md)。

## 当前架构

```text
AHT20 ─┐
INA219 ├─ I2C1 ─ STM32 + FreeRTOS ─ USART2 ─ ESP32 ─ Wi-Fi/MQTT
OLED  ─┘                │                         │
                       └─ USART1 ─ RS485/Modbus   └─ USB串口 ─ Qt上位机
```

STM32 FreeRTOS任务：

- `COMM`：UART心跳、ACK、超时重传和遥测帧；
- `SENSOR`：AHT20/INA219采集；
- `MODBUS`：RS485/Modbus RTU主站轮询；
- `DISPLAY`：SSD1306 OLED刷新；
- `SUPERVISOR`：检查各任务活性，仅在全部任务健康时喂独立看门狗。

## 主要引脚

| 功能 | STM32引脚 |
|---|---|
| OLED/AHT20/INA219 SCL | PB6 / I2C1 SCL |
| OLED/AHT20/INA219 SDA | PB7 / I2C1 SDA |
| STM32 → ESP32 | PA2 → GPIO16 |
| ESP32 → STM32 | GPIO17 → PA3 |
| RS485 DI / TX | PA9 / USART1 TX |
| RS485 RO / RX | PA10 / USART1 RX |
| RS485 DE + `/RE` | PB1 |
| ST-Link SWDIO/SWCLK | PA13 / PA14 |

## 工程入口

- STM32 Keil工程：`MDK-ARM/industrial_gateway.uvprojx`
- ESP-IDF工程：`esp32_gateway/`
- 协议说明：`docs/串口与MQTT协议_v1.0.md`
- 待测试流程：`docs/预备实现_v0.6_RS485_WiFi_MQTT.md`
- 看门狗与浅睡眠：`docs/设计与实验流程_v0.7_看门狗与安全浅睡眠.md`
- 实验记录索引：`docs/实验记录索引.md`
- Modbus RTU端到端成功记录：`docs/实验记录_v0.9_Modbus_RTU端到端读取成功.md`
- AHT20/INA219风扇负载记录：`docs/实验记录_v1.0_AHT20_INA219_5V风扇负载测量.md`
- Qt上位机工程：`qt_upper_computer/`
- Qt端到端监控记录：`docs/实验记录_v1.2_Qt上位机端到端监控.md`
- ACK断线故障注入：`docs/实验记录_v1.3_STM32_ESP32_ACK回传断线故障注入.md`
- Modbus断线与恢复：`docs/实验记录_v1.4_Modbus接收链路断线与自动恢复.md`
- Wi-Fi/MQTT断线与恢复：`docs/实验记录_v1.5_WiFi_MQTT断线与自动重连.md`

涉及ESP32烧录和监视时，默认使用 VS Code 总命令：

```text
ESP-IDF: Build, Flash and Monitor your Device
```

## 当前边界

FreeRTOS迁移、UART双向ACK、传感器采集、RS485/Modbus主站、Wi-Fi/MQTT以及Qt上位机均已完成端到端实机验证，并形成正常状态、断线状态和自动恢复状态的证据链。

当前原型能够完成“现场采集—本地显示—工业总线—无线传输—PC可视化”的完整闭环。更长时间稳定性、启动瞬态、测量精度对比和执行器闭环控制仍可继续补充；继电器/MOSFET、外壳、PCB、离线存储和有线以太网暂未纳入当前版本。
