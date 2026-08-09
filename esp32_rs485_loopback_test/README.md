# ESP32 RS485 loopback diagnostic

This is a temporary, standalone ESP-IDF diagnostic project. It does not
replace or modify `esp32_gateway`.

- UART2 RX: GPIO16, connected directly to STM32 PA9
- UART2 TX: GPIO17, connected to the transmitter RS485 module RXD
- UART format: 9600 8N1
- Expected request: `01 03 00 00 00 02 C4 0B`
- Fixed response: `01 03 04 04 D2 16 2E D5 46`
