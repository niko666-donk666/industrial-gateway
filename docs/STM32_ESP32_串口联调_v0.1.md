# STM32 ↔ ESP32 串口联调 v0.1

## 接线（断电后完成）

| STM32F103C8T6 | ESP32 DevKit | 说明 |
| --- | --- | --- |
| PA2（USART2_TX） | RX2 / GPIO16 | STM32 向 ESP32 发数据 |
| PA3（USART2_RX） | TX2 / GPIO17 | ESP32 向 STM32 回数据，v0.1 暂未使用 |
| GND | GND | 必须共地 |

不要连接两块板的 3.3V、5V、VIN 电源脚。

## 供电

```text
笔记本 USB ① → ST-Link → STM32
笔记本 USB ② → ESP32 Micro-USB
```

STM32 由 ST-Link 的 3.3V 供电；ESP32 由自身 Micro-USB 供电。两块板仅通过 GND 建立信号参考地。

## STM32 固件行为

当前 `App/Src/app_main.c` 每 1 秒向 USART2 发出：

```text
GW,HEARTBEAT\r\n
```

参数：115200 bps、8 数据位、无校验、1 停止位（8N1）。

## ESP32 首次验收

ESP32 后续固件需使用 UART2：

```text
RX = GPIO16
TX = GPIO17
波特率 = 115200
```

成功条件：ESP32 串口监视器每秒输出一次 `GW,HEARTBEAT`。

## 后续演进

物理链路验证成功后，协议将升级为二进制帧：帧头、类型、长度、序号、CRC16、ACK 和超时重传。
