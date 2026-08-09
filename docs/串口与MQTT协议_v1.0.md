# STM32、ESP32 与 MQTT 协议 v1.0

## 串口参数

- STM32 USART2 ↔ ESP32 UART2
- 115200 baud、8 data bits、no parity、1 stop bit
- ASCII 行协议，以 `\r\n` 结束

## 心跳与确认

```text
GW,HB,<seq>\r\n
ESP,ACK,<seq>\r\n
```

STM32 每约 1 秒发送心跳。500 ms 内未收到同序号 ACK 时重发同一帧，最多重传 3 次。

## 传感器遥测帧

```text
GW,DATA,<seq>,<aht_ok>,<temp_dC>,<hum_dPct>,<ina_ok>,<bus_mv>,<current_ma>,<power_mw>\r\n
```

| 字段 | 单位/含义 |
|---|---|
| `seq` | 遥测序号，1～65535 循环 |
| `aht_ok` | AHT20 有效标志，0/1 |
| `temp_dC` | 0.1 ℃，例如 `253` 表示 25.3 ℃ |
| `hum_dPct` | 0.1 %RH，例如 `481` 表示 48.1 %RH |
| `ina_ok` | INA219 有效标志，0/1 |
| `bus_mv` | 母线电压，mV |
| `current_ma` | 电流，mA，可为负值 |
| `power_mw` | 功率绝对值，mW |

示例：

```text
GW,DATA,18,1,253,481,1,5020,123,617
```

ESP32 发布到 `industrial_gateway/<device-id>/telemetry`：

```json
{"device_id":"industrial-gateway-001","sequence":18,"aht20_online":true,"temperature_c":25.3,"humidity_percent":48.1,"ina219_online":true,"bus_voltage_v":5.02,"current_ma":123,"power_mw":617}
```

## Modbus 状态帧

```text
GW,MB,<seq>,<online>,<slave>,<exception>,<reg0>,<reg1>,<success>,<errors>\r\n
```

当前预备配置为 Modbus RTU 从站地址 1、功能码 `0x03`、起始寄存器 `0x0000`、读取 2 个保持寄存器。ESP32 将其发布到 `industrial_gateway/<device-id>/modbus`。

Modbus 寄存器的工程含义必须根据以后接入设备的说明书定义；当前 `reg0`、`reg1` 只是原始 16 位数值，不能提前解释成温度、转速或故障码。
