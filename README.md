# 基于 FreeRTOS 的工业设备远程运维与故障诊断终端

本项目是一套已经完成实机联调的工业网关原型，采用 **STM32F103C8T6 + ESP32-WROOM-32 + Qt 6/C++** 架构。系统将现场传感器和 Modbus 设备的数据统一采集，通过 OLED 就地显示、Wi-Fi/MQTT 远程上报，并在 Qt 上位机中集中展示设备状态、实时数据和通信日志。

项目已完成传感器采集、双板通信、Modbus RTU轮询、MQTT上报、Qt可视化，以及 UART、Modbus、Wi-Fi/MQTT 三类断线与自动恢复实验。完整证据见 [实验记录索引](docs/实验记录索引.md)。

## 它解决什么问题

工业现场的数据通常分散在传感器、仪表和控制器中，接口与协议不统一，发生故障时也难以快速判断问题位于采集端、现场总线、网络还是上位机。

本项目提供一条完整的数据与诊断链路：

- 统一采集温湿度、电压、电流、功率和 Modbus 寄存器数据；
- 通过 FreeRTOS 将通信、采集、显示和系统监控任务解耦；
- 使用心跳、ACK、超时重传、在线状态和错误计数定位通信异常；
- 网络中断时保留本地采集和 OLED 显示，网络恢复后自动继续上报；
- 在 Qt 上位机中集中查看遥测数据、Modbus状态和通信过程。

## 谁会使用它

- 设备运维人员：查看设备在线状态、负载参数和异常记录；
- 现场调试人员：确认传感器、RS485、双板串口和网络链路是否正常；
- 自动化与嵌入式工程师：作为小型工业采集网关、协议转换终端或远程监测终端的参考实现。

## 支持的设备与协议

| 类别 | 当前支持 |
|---|---|
| 主控制器 | STM32F103C8T6，运行 FreeRTOS |
| 网络协处理器 | ESP32-WROOM-32，ESP-IDF 5.2.7 |
| 环境传感器 | AHT20 温湿度传感器 |
| 电参量传感器 | INA219 电压、电流、功率监测 |
| 本地显示 | SSD1306 I2C OLED |
| 工业通信 | RS485、Modbus RTU主站、功能码 `0x03` |
| 双板通信 | UART ASCII帧、心跳、ACK、超时重传 |
| 网络通信 | Wi-Fi Station、MQTT、JSON |
| PC端 | Qt 6/C++上位机、USB串口接入 |

Modbus RTU当前按从站地址 `1`、起始地址 `0x0000`读取两个保持寄存器。接入其他工业仪表时，可根据设备手册调整从站地址、寄存器地址和数据解释方式。

## 数据从哪里来，最后送到哪里

```text
AHT20 ───────────────┐
INA219 ───── I2C ────┤
                     v
Modbus RTU从站 ─ RS485 ─> STM32 + FreeRTOS ─ UART ─> ESP32
                              │                       │
                              └─> SSD1306 OLED        ├─> Wi-Fi/MQTT Broker
                                                      └─> USB串口 ─> Qt上位机
```

数据流说明：

1. STM32通过I2C读取AHT20和INA219，通过RS485轮询Modbus RTU从站；
2. STM32将传感器数据、Modbus结果和链路状态编码为UART帧发送给ESP32；
3. ESP32解析数据并转换为JSON，发布到MQTT Broker；
4. Qt上位机读取ESP32 USB串口日志，显示温湿度、电参量、寄存器值、成功/错误计数和通信事件；
5. OLED提供不依赖电脑和网络的现场状态显示。

MQTT主题：

```text
industrial_gateway/<device-id>/status
industrial_gateway/<device-id>/telemetry
industrial_gateway/<device-id>/modbus
```

详细帧格式见 [STM32、ESP32与MQTT协议](docs/串口与MQTT协议_v1.0.md)。

## 最简单的启动方式

### 1. 启动STM32

使用Keil打开：

```text
MDK-ARM/industrial_gateway.uvprojx
```

通过ST-Link将程序下载到STM32。STM32负责FreeRTOS任务、传感器采集、Modbus轮询、OLED显示和系统看门狗。

### 2. 启动ESP32

使用VS Code打开 `esp32_gateway/`。首次运行时在menuconfig的 `Industrial gateway network configuration` 中填写Wi-Fi、MQTT Broker和设备ID，然后执行总命令：

```text
ESP-IDF: Build, Flash and Monitor your Device
```

### 3. 启动Qt上位机

1. 关闭ESP-IDF Monitor，释放ESP32对应的COM端口；
2. 使用Qt Creator打开 `qt_upper_computer/CMakeLists.txt`；
3. 选择 `Desktop Qt 6.11.1 MinGW 64-bit` 套件并运行；
4. 在上位机中选择ESP32串口，波特率设为 `115200`，点击“连接”。

连接成功后，上位机会实时显示传感器数据、Modbus状态和通信日志。

## 硬件连接

| 功能 | 连接方式 |
|---|---|
| OLED/AHT20/INA219 SCL | STM32 PB6 / I2C1 SCL |
| OLED/AHT20/INA219 SDA | STM32 PB7 / I2C1 SDA |
| STM32发送至ESP32 | PA2 → GPIO16 |
| ESP32发送至STM32 | GPIO17 → PA3 |
| RS485 DI / TX | STM32 PA9 / USART1 TX |
| RS485 RO / RX | STM32 PA10 / USART1 RX |
| RS485 DE与`/RE` | 短接后连接STM32 PB1 |
| ST-Link SWDIO/SWCLK | STM32 PA13 / PA14 |

STM32与ESP32分别通过USB供电，只连接UART信号线和GND，不互接两块开发板的3.3V或5V。

## 软件结构

STM32 FreeRTOS任务：

- `COMM`：UART心跳、ACK、超时重传和遥测帧；
- `SENSOR`：AHT20与INA219采集；
- `MODBUS`：RS485/Modbus RTU主站轮询；
- `DISPLAY`：SSD1306 OLED刷新；
- `SUPERVISOR`：检查任务活性，仅在全部任务健康时喂独立看门狗。

仓库主要目录：

```text
MDK-ARM/             STM32 Keil工程
Core/                STM32应用代码
Middlewares/         FreeRTOS中间件
esp32_gateway/       ESP-IDF联网协处理器工程
qt_upper_computer/   Qt 6/C++上位机
docs/                协议、设计说明与实机实验记录
hardware/            BOM与硬件资料
```

## 已验证结果

- STM32与ESP32心跳序号连续递增并逐帧ACK；
- AHT20、INA219和OLED完成实机采集与显示；
- Modbus RTU主站成功读取保持寄存器 `1234`、`5678`；
- ESP32成功通过Wi-Fi/MQTT发布遥测和Modbus JSON；
- Qt上位机成功显示温湿度、电压、电流、功率、寄存器和累计计数；
- UART ACK、Modbus接收链路和Wi-Fi/MQTT断线后均可自动恢复；
- FreeRTOS多任务运行期间，传感器、Modbus、UART和网络链路可以独立工作并报告状态。
