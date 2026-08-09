# FreeRTOS 串口接收链路复盘题

## 第 1 轮：中断与环形缓冲区

结合 `App/Src/app_main.c` 回答：

1. `App_Init()` 中为什么要先调用一次 `HAL_UART_Receive_IT()`？它让 HAL 接下来接收几个字节，接收到哪里？
2. 一个字节到达 USART2 后，`HAL_UART_RxCpltCallback()` 主要完成哪三件事？为什么最后还要再次调用 `HAL_UART_Receive_IT()`？
3. `s_esp_rx_head`（写入位置）和 `s_esp_rx_tail`（取出位置）分别由谁推进？两者相等表示什么？
4. 当 `next == s_esp_rx_tail` 时表示什么？当前代码如何处理新收到的字节？
5. 为什么中断回调只保存字节，不在里面直接使用 `sscanf()` 解析 ACK？
6. `EspRxPop()` 返回 `1U` 和 `0U` 分别表示什么？它取出的字节通过哪个参数交给调用者？
7. `s_esp_rx_ring` 和 `link.rx_line` 的职责有什么区别？

## 面试要求

不要求背诵取模表达式，但需要能画出并讲清：

`USART2 收到字节 → 中断回调写环形缓冲区 → CommunicationTask 调用 PollEspUart → EspRxPop 取字节 → rx_line 拼帧 → 解析 ACK`
