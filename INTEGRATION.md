# v0.1 固件集成

在 CubeMX 用 `industrial_gateway.ioc` 生成 Keil 工程后：

1. 在 Keil 中创建 `App` 分组，加入 `App/Src/app_main.c` 与 `App/Src/ssd1306.c`。
2. 在 `Options for Target -> C/C++ -> Include Paths` 添加：`..\\App\\Inc`。
3. 编辑 `Core/Src/main.c`：

```c
/* USER CODE BEGIN Includes */
#include "app_main.h"
/* USER CODE END Includes */
```

4. 在 `MX_GPIO_Init();`、`MX_I2C1_Init();`、`MX_USART2_UART_Init();` 后加入：

```c
App_Init();
```

5. 在 `while (1)` 中加入：

```c
App_RunOnce();
```

v0.1 的验收结果：PC13 板载 LED 每 500 ms 翻转；OLED 显示 `IND GW V0.1`、`CORE: ONLINE`、`NET: WAIT ESP`。

若 OLED 仍为空白，先用 `HAL_I2C_IsDeviceReady` 的返回值确认地址；该驱动默认使用常见地址 `0x3C`。
