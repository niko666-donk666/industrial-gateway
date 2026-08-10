# 工业网关 Qt 上位机

这是与STM32、ESP32固件配套的Qt 6/C++桌面监控程序，通过ESP32 USB串口接收网关日志和业务数据，集中显示传感器遥测、Modbus状态与通信事件。

## 已实现

- Qt Widgets 工业风格仪表盘；
- 自动枚举 Windows 串口；
- 读取 ESP32 USB 串口日志；
- 解析日志中 `payload={...}` 后的 MQTT JSON；
- 直接解析 `GW,DATA,...` 和 `GW,MB,...` 原始协议帧；
- 显示 AHT20 温湿度、INA219 电压/电流/功率；
- 显示 Modbus 在线状态、寄存器值和成功/错误计数；
- 内置演示数据，未连接硬件也能验证界面。

## 构建环境

- Qt 6.11.1
- Qt Creator 20.0.1
- MinGW 13.1.0 64-bit
- CMake + Ninja
- Qt Serial Port

使用 Qt Creator 打开本目录的 `CMakeLists.txt`，选择 `Desktop Qt 6.11.1 MinGW 64-bit` 套件后运行。

## 串口使用

1. 关闭 ESP-IDF Monitor，避免占用 ESP32 的 COM 端口；
2. 在上位机中选择 ESP32 对应端口和 `115200`；
3. 点击“连接”；
4. ESP32 输出的 MQTT 发布日志或 STM32 原始数据帧会驱动仪表盘更新。
