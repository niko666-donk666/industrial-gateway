#include "main.h"
#include "app_main.h"
#include "modbus_rtu.h"
#include "sensors.h"
#include "ssd1306.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

#define HEARTBEAT_PERIOD_MS       1000U
#define ACK_TIMEOUT_MS             500U
#define MAX_RETRY_COUNT              3U
#define LINK_OFFLINE_MS            3000U
#define COMM_TASK_PERIOD_MS          10U
#define SENSOR_PERIOD_MS           2000U
#define DISPLAY_PERIOD_MS           500U
#define TELEMETRY_PERIOD_MS        2000U
#define MODBUS_POLL_PERIOD_MS      1000U
#define MODBUS_SLAVE_ADDRESS          1U
#define MODBUS_START_REGISTER         0U
#define MODBUS_REGISTER_COUNT         2U
#define ESP_RX_RING_SIZE             64U
#define WATCHDOG_PERIOD_MS           500U
#define WATCHDOG_COMM_DEADLINE_MS   1000U
#define WATCHDOG_SENSOR_DEADLINE_MS 5000U
#define WATCHDOG_DISPLAY_DEADLINE_MS 2000U
#define WATCHDOG_MODBUS_DEADLINE_MS  4000U
#define IWDG_RELOAD_VALUE            1249U

#define COMM_TASK_STACK_WORDS       256U
#define SENSOR_TASK_STACK_WORDS     192U
#define DISPLAY_TASK_STACK_WORDS    256U
#define MODBUS_TASK_STACK_WORDS     192U
#define WATCHDOG_TASK_STACK_WORDS   160U

#define COMM_TASK_PRIORITY          (tskIDLE_PRIORITY + 3U)
#define SENSOR_TASK_PRIORITY        (tskIDLE_PRIORITY + 2U)
#define DISPLAY_TASK_PRIORITY       (tskIDLE_PRIORITY + 1U)
#define MODBUS_TASK_PRIORITY        (tskIDLE_PRIORITY + 2U)
#define WATCHDOG_TASK_PRIORITY      (tskIDLE_PRIORITY + 4U)

typedef enum {
    HEALTH_COMM = 0,
    HEALTH_SENSOR,
    HEALTH_DISPLAY,
    HEALTH_MODBUS,
    HEALTH_COUNT
} HealthTask;

typedef struct {
    SensorData sensors;
    ModbusData modbus;
    uint32_t ack_count;
    uint32_t timeout_count;
    uint8_t esp_online;
    uint8_t last_reset_iwdg;
} GatewayStatus;//网关共享状态

typedef struct {
    TickType_t last_tx_tick;
    TickType_t last_ack_tick;
    TickType_t last_led_tick;
    uint16_t next_seq;//下一次心跳序号
    uint16_t pending_seq;//待处理心跳序号
    uint8_t retry_count;
    uint8_t waiting_ack;
    uint8_t ack_seen;//是否曾经收到过有效ack
    uint8_t rx_line[32];
    uint8_t rx_length;
    uint16_t telemetry_seq;
    TickType_t last_telemetry_tick;
} LinkContext;//esp32通信链路上下文

static GatewayStatus s_status;
static SemaphoreHandle_t s_status_mutex;//共享状态互斥量
static SemaphoreHandle_t s_i2c_mutex;//I2C总线互斥量
static uint8_t s_esp_rx_ring[ESP_RX_RING_SIZE];
static uint8_t s_esp_rx_irq_byte;
static volatile uint16_t s_esp_rx_head;
static volatile uint16_t s_esp_rx_tail;
static volatile TickType_t s_task_health[HEALTH_COUNT];
static IWDG_HandleTypeDef s_iwdg;

static void TaskHealthMark(HealthTask task)
{
    s_task_health[task] = xTaskGetTickCount();
}

static uint8_t EspRxPop(uint8_t *byte)//操作尾指针把环形缓冲区的数据一个字节一个字节地读出来
{
    uint16_t tail = s_esp_rx_tail;

    if (tail == s_esp_rx_head) {
        return 0U;
    }
    *byte = s_esp_rx_ring[tail];
    s_esp_rx_tail = (uint16_t)((tail + 1U) % ESP_RX_RING_SIZE);
    return 1U;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        uint16_t head = s_esp_rx_head;
        uint16_t next = (uint16_t)((head + 1U) % ESP_RX_RING_SIZE);

        if (next != s_esp_rx_tail) {
            s_esp_rx_ring[head] = s_esp_rx_irq_byte;
            s_esp_rx_head = next;
        }
        (void)HAL_UART_Receive_IT(&huart2, &s_esp_rx_irq_byte, 1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        __HAL_UART_CLEAR_OREFLAG(huart);
        (void)HAL_UART_Receive_IT(&huart2, &s_esp_rx_irq_byte, 1U);
    }
}

static void StatusLock(void)
{
    (void)xSemaphoreTake(s_status_mutex, portMAX_DELAY);
}

static void StatusUnlock(void)
{
    (void)xSemaphoreGive(s_status_mutex);
}

static void TransmitHeartbeat(LinkContext *link, uint16_t sequence)
{
    char frame[24];//存储一帧通信报文
    int length = snprintf(frame, sizeof(frame), "GW,HB,%u\r\n", (unsigned int)sequence);//snprintf生成GW,HB,<seq>

    if ((length > 0) && ((size_t)length < sizeof(frame))) {
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)frame, (uint16_t)length, 20U);
        link->last_tx_tick = xTaskGetTickCount();
    }
}

static void StartHeartbeat(LinkContext *link)
{
    link->next_seq++;//产生新序号
    if (link->next_seq == 0U) {
        link->next_seq = 1U;
    }

    link->pending_seq = link->next_seq;
    link->retry_count = 0U;
    link->waiting_ack = 1U;//设置等待ACK
    TransmitHeartbeat(link, link->pending_seq);
}

static void TransmitTelemetry(LinkContext *link, const GatewayStatus *snapshot)
{
    char frame[128];
    int length;

    link->telemetry_seq++;
    if (link->telemetry_seq == 0U) link->telemetry_seq = 1U;

    length = snprintf(frame, sizeof(frame),
                      "GW,DATA,%u,%u,%d,%u,%u,%u,%d,%u\r\n",
                      (unsigned int)link->telemetry_seq,
                      snapshot->sensors.aht20_ready ? 1U : 0U,
                      (int)snapshot->sensors.temperature_deci_c,
                      (unsigned int)snapshot->sensors.humidity_deci_percent,
                      snapshot->sensors.ina219_ready ? 1U : 0U,
                      (unsigned int)snapshot->sensors.bus_voltage_mv,
                      (int)snapshot->sensors.current_ma,
                      (unsigned int)snapshot->sensors.power_mw);
    if ((length > 0) && ((size_t)length < sizeof(frame))) {
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)frame, (uint16_t)length, 30U);
    }

    length = snprintf(frame, sizeof(frame),
                      "GW,MB,%u,%u,%u,%u,%u,%u,%lu,%lu\r\n",
                      (unsigned int)link->telemetry_seq,
                      snapshot->modbus.online ? 1U : 0U,
                      (unsigned int)snapshot->modbus.slave_address,
                      (unsigned int)snapshot->modbus.exception_code,
                      (unsigned int)snapshot->modbus.registers[0],
                      (unsigned int)snapshot->modbus.registers[1],
                      (unsigned long)snapshot->modbus.success_count,
                      (unsigned long)snapshot->modbus.error_count);
    if ((length > 0) && ((size_t)length < sizeof(frame))) {
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)frame, (uint16_t)length, 30U);
    }
}

static void RecordMatchedAck(LinkContext *link)
{
    link->last_ack_tick = xTaskGetTickCount();
    link->ack_seen = 1U;
    link->waiting_ack = 0U;

    StatusLock();
    s_status.ack_count++;
    StatusUnlock();
}

static void PollEspUart(LinkContext *link)//把link中符合要求的数据放进环形缓冲区
{
    uint8_t byte;

    while (EspRxPop(&byte)) {
        if (byte == '\r') {
            continue;
        }
        if (byte == '\n') {
            unsigned int sequence;

            link->rx_line[link->rx_length] = '\0';
            if ((sscanf((const char *)link->rx_line, "ESP,ACK,%u", &sequence) == 1) &&
                link->waiting_ack && ((uint16_t)sequence == link->pending_seq)) {
                RecordMatchedAck(link);
            }
            link->rx_length = 0U;
            continue;
        }
        if (link->rx_length < (sizeof(link->rx_line) - 1U)) {
            link->rx_line[link->rx_length++] = byte;
        } else {
            link->rx_length = 0U;
        }
    }
}

static uint8_t IsEspOnline(const LinkContext *link, TickType_t now)
{
    return (link->ack_seen &&
            ((now - link->last_ack_tick) < pdMS_TO_TICKS(LINK_OFFLINE_MS))) ? 1U : 0U;
}

static void CommunicationTask(void *argument)
{
    LinkContext link;//通信链路私有状态
    TickType_t wake_tick;

    (void)argument;
    memset(&link, 0, sizeof(link));
    link.last_tx_tick = xTaskGetTickCount();
    link.last_led_tick = link.last_tx_tick;
    link.last_telemetry_tick = link.last_tx_tick;
    wake_tick = link.last_tx_tick;
    StartHeartbeat(&link);

    for (;;) {
        TickType_t now;
        uint8_t esp_online;
        GatewayStatus snapshot;

        PollEspUart(&link);
        now = xTaskGetTickCount();

        if (link.waiting_ack && ((now - link.last_tx_tick) >= pdMS_TO_TICKS(ACK_TIMEOUT_MS))) {
            if (link.retry_count < MAX_RETRY_COUNT) {
                link.retry_count++;
                TransmitHeartbeat(&link, link.pending_seq);
            } else {
                link.waiting_ack = 0U;
                StatusLock();
                s_status.timeout_count++;
                StatusUnlock();
            }
        }

        if (!link.waiting_ack &&
            ((now - link.last_tx_tick) >= pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS))) {
            StartHeartbeat(&link);
        }

        esp_online = IsEspOnline(&link, now);
        StatusLock();
        s_status.esp_online = esp_online;
        StatusUnlock();

        if ((now - link.last_telemetry_tick) >= pdMS_TO_TICKS(TELEMETRY_PERIOD_MS)) {
            link.last_telemetry_tick = now;
            StatusLock();
            snapshot = s_status;
            StatusUnlock();
            TransmitTelemetry(&link, &snapshot);
        }

        if ((now - link.last_led_tick) >= pdMS_TO_TICKS(esp_online ? 1000U : 250U)) {
            link.last_led_tick = now;
            HAL_GPIO_TogglePin(HEARTBEAT_LED_GPIO_Port, HEARTBEAT_LED_Pin);
        }

        TaskHealthMark(HEALTH_COMM);
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(COMM_TASK_PERIOD_MS));
    }
}

static void ModbusTask(void *argument)
{
    TickType_t wake_tick = xTaskGetTickCount();
    ModbusData sample;

    (void)argument;
    memset(&sample, 0, sizeof(sample));
    sample.slave_address = MODBUS_SLAVE_ADDRESS;
    sample.register_count = MODBUS_REGISTER_COUNT;

    for (;;) {
        (void)Modbus_ReadHoldingRegisters(MODBUS_SLAVE_ADDRESS,
                                         MODBUS_START_REGISTER,
                                         MODBUS_REGISTER_COUNT,
                                         &sample);
        StatusLock();
        s_status.modbus = sample;
        StatusUnlock();
        TaskHealthMark(HEALTH_MODBUS);
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(MODBUS_POLL_PERIOD_MS));
    }
}

static void SensorTask(void *argument)
{
    TickType_t wake_tick = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        SensorData sample;

        memset(&sample, 0, sizeof(sample));
        (void)xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
        Sensors_Read(&sample);
        (void)xSemaphoreGive(s_i2c_mutex);

        StatusLock();
        s_status.sensors = sample;
        StatusUnlock();

        TaskHealthMark(HEALTH_SENSOR);
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

static void FormatDisplay(const GatewayStatus *snapshot, char *climate, char *electrical)//生成现场巡检字符串
{
    if (snapshot->sensors.aht20_ready) {
        int16_t temperature = snapshot->sensors.temperature_deci_c;
        int16_t decimal = (temperature < 0 ? -temperature : temperature) % 10;

        (void)snprintf(climate, 22U, "T:%d.%dC H:%u.%u%%",
                       temperature / 10, decimal,
                       snapshot->sensors.humidity_deci_percent / 10U,
                       snapshot->sensors.humidity_deci_percent % 10U);
    } else {
        (void)snprintf(climate, 22U, "T:--.-C H:--.-%%");
    }

    if (snapshot->sensors.ina219_ready) {
        (void)snprintf(electrical, 22U, "V:%u.%02u I:%dmA",
                       snapshot->sensors.bus_voltage_mv / 1000U,
                       (snapshot->sensors.bus_voltage_mv % 1000U) / 10U,
                       snapshot->sensors.current_ma);
    } else {
        (void)snprintf(electrical, 22U, "V:--.-- I:---mA");
    }
}

static void DisplayTask(void *argument)
{
    TickType_t wake_tick = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        GatewayStatus snapshot;//共享状态快照
        char link_status[22];//ESP32与Modbus摘要
        char climate[22];//温湿度显示字符串
        char electrical[22];//电压电流显示字符串
        char power_reset[22];//功率与最近启动原因
        char modbus_values[22];//从站与寄存器值
        char modbus_counts[22];//Modbus成功和错误统计
        char link_counts[22];//双板ACK与超时统计

        StatusLock();
        snapshot = s_status;
        StatusUnlock();

        FormatDisplay(&snapshot, climate, electrical);
        (void)snprintf(link_status, sizeof(link_status), "ESP:%s MB:%s",
                       snapshot.esp_online ? "ON" : "OFF",
                       snapshot.modbus.online ? "ON" : "OFF");
        if (snapshot.sensors.ina219_ready) {
            (void)snprintf(power_reset, sizeof(power_reset), "P:%umW BOOT:%s",
                           (unsigned int)snapshot.sensors.power_mw,
                           snapshot.last_reset_iwdg ? "IWDG" : "NORM");
        } else {
            (void)snprintf(power_reset, sizeof(power_reset), "P:---mW BOOT:%s",
                           snapshot.last_reset_iwdg ? "IWDG" : "NORM");
        }
        if (snapshot.modbus.online) {
            (void)snprintf(modbus_values, sizeof(modbus_values), "S%u R0:%u R1:%u",
                           (unsigned int)snapshot.modbus.slave_address,
                           (unsigned int)snapshot.modbus.registers[0],
                           (unsigned int)snapshot.modbus.registers[1]);
        } else {
            (void)snprintf(modbus_values, sizeof(modbus_values), "S%u E%u RX:%u/%u",
                           (unsigned int)snapshot.modbus.slave_address,
                           (unsigned int)snapshot.modbus.last_error,
                           (unsigned int)snapshot.modbus.received_bytes,
                           (unsigned int)snapshot.modbus.expected_bytes);
        }
        (void)snprintf(modbus_counts, sizeof(modbus_counts), "MB OK:%lu ERR:%lu",
                       (unsigned long)snapshot.modbus.success_count,
                       (unsigned long)snapshot.modbus.error_count);
        (void)snprintf(link_counts, sizeof(link_counts), "ACK:%lu TO:%lu",
                       (unsigned long)snapshot.ack_count,
                       (unsigned long)snapshot.timeout_count);

        (void)xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
        if (SSD1306_IsReady()) {
            SSD1306_Clear();
            SSD1306_WriteString(0U, "IND GW LOCAL");
            SSD1306_WriteString(1U, link_status);
            SSD1306_WriteString(2U, climate);
            SSD1306_WriteString(3U, electrical);
            SSD1306_WriteString(4U, power_reset);
            SSD1306_WriteString(5U, modbus_values);
            SSD1306_WriteString(6U, modbus_counts);
            SSD1306_WriteString(7U, link_counts);
            SSD1306_Update();
        }
        (void)xSemaphoreGive(s_i2c_mutex);

        TaskHealthMark(HEALTH_DISPLAY);
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
    }
}

static uint8_t TaskIsHealthy(HealthTask task, TickType_t now, uint32_t deadline_ms)
{
    return ((now - s_task_health[task]) <= pdMS_TO_TICKS(deadline_ms)) ? 1U : 0U;
}

static void WatchdogTask(void *argument)
{
    TickType_t wake_tick = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        uint8_t all_healthy =
            TaskIsHealthy(HEALTH_COMM, now, WATCHDOG_COMM_DEADLINE_MS) &&
            TaskIsHealthy(HEALTH_SENSOR, now, WATCHDOG_SENSOR_DEADLINE_MS) &&
            TaskIsHealthy(HEALTH_DISPLAY, now, WATCHDOG_DISPLAY_DEADLINE_MS) &&
            TaskIsHealthy(HEALTH_MODBUS, now, WATCHDOG_MODBUS_DEADLINE_MS);

        if (all_healthy) {
            (void)HAL_IWDG_Refresh(&s_iwdg);
        }
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(WATCHDOG_PERIOD_MS));
    }
}

static void WatchdogInit(void)
{
    s_iwdg.Instance = IWDG;
    s_iwdg.Init.Prescaler = IWDG_PRESCALER_256;
    s_iwdg.Init.Reload = IWDG_RELOAD_VALUE;
    if (HAL_IWDG_Init(&s_iwdg) != HAL_OK) {
        Error_Handler();
    }
}

void App_Init(void)
{
    TickType_t initial_tick;

    memset(&s_status, 0, sizeof(s_status));//清零网关状态
    s_status.last_reset_iwdg = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) ? 1U : 0U;//随后检查上次是否由IWDG复位
    __HAL_RCC_CLEAR_RESET_FLAGS();

    s_status_mutex = xSemaphoreCreateMutex();//创建状态互斥量
    s_i2c_mutex = xSemaphoreCreateMutex();//创建I2C互斥量
    if ((s_status_mutex == NULL) || (s_i2c_mutex == NULL)) {
        Error_Handler();
    }

    s_esp_rx_head = 0U;
    s_esp_rx_tail = 0U;
    if (HAL_UART_Receive_IT(&huart2, &s_esp_rx_irq_byte, 1U) != HAL_OK) {//启动USART2单字节中断接收
        Error_Handler();
    }

    initial_tick = xTaskGetTickCount();//初始化任务健康时间
    for (uint32_t index = 0U; index < HEALTH_COUNT; index++) {
        s_task_health[index] = initial_tick;
    }

    Sensors_Init();//传感器初始化
    Modbus_Init();//初始化Modbus
    if (SSD1306_Init()) { //初始化OLED
        SSD1306_Clear();
        SSD1306_WriteString(0U, "IND GW LOCAL");
        SSD1306_WriteString(1U, "BOOT: INITIALIZING");
        SSD1306_WriteString(2U, "SENSORS: STARTING");
        SSD1306_WriteString(3U, "ESP/MB: WAIT");
        SSD1306_Update();
    }
//创建应用任务与统一健康监控任务
    if ((xTaskCreate(CommunicationTask, "COMM", COMM_TASK_STACK_WORDS, NULL,
                     COMM_TASK_PRIORITY, NULL) != pdPASS) ||
        (xTaskCreate(SensorTask, "SENSOR", SENSOR_TASK_STACK_WORDS, NULL,
                     SENSOR_TASK_PRIORITY, NULL) != pdPASS) ||
        (xTaskCreate(DisplayTask, "DISPLAY", DISPLAY_TASK_STACK_WORDS, NULL,
                     DISPLAY_TASK_PRIORITY, NULL) != pdPASS) ||
        (xTaskCreate(ModbusTask, "MODBUS", MODBUS_TASK_STACK_WORDS, NULL,
                     MODBUS_TASK_PRIORITY, NULL) != pdPASS) ||
        (xTaskCreate(WatchdogTask, "SUPERVISOR", WATCHDOG_TASK_STACK_WORDS, NULL,
                     WATCHDOG_TASK_PRIORITY, NULL) != pdPASS)) {
        Error_Handler();
    }

    WatchdogInit();//初始化IWDG
}

void App_StartScheduler(void)
{
    vTaskStartScheduler();//Freertos开始调度任务
    Error_Handler();//错误返回
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

void vApplicationIdleHook(void)
{
    __WFI();
}
