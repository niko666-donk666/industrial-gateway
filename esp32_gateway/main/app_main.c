#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
 
#define GATEWAY_UART        UART_NUM_2
#define GATEWAY_UART_TX_PIN GPIO_NUM_17
#define GATEWAY_UART_RX_PIN GPIO_NUM_16
#define GATEWAY_UART_BAUD   115200
#define RX_BUFFER_SIZE      512
#define LINE_BUFFER_SIZE    192
#define TOPIC_BUFFER_SIZE    96

#define WIFI_CONNECTED_BIT BIT0//WIFI_CONNECTED_BIT = 1 ESP32已连接Wi-Fi并获得IP
#define MQTT_CONNECTED_BIT BIT1//MQTT_CONNECTED_BIT = 1 ESP32内部的MQTT客户端已连接Broker

typedef struct {
    unsigned int sequence;
    bool aht20_ready;
    int temperature_deci_c;
    unsigned int humidity_deci_percent;
    bool ina219_ready;
    unsigned int bus_voltage_mv;
    int current_ma;
    unsigned int power_mw;
    bool valid;//这个结构体中是否已经保存过一帧成功解析的数据
} TelemetryRecord;//传感器数据记录

typedef struct {
    unsigned int sequence;
    bool online;
    unsigned int slave_address;
    unsigned int exception_code;
    unsigned int register0;
    unsigned int register1;
    unsigned long success_count;
    unsigned long error_count;
    bool valid;
} ModbusRecord;

static const char *TAG = "gateway";
static EventGroupHandle_t s_network_bits;
static esp_mqtt_client_handle_t s_mqtt_client;
static bool s_mqtt_started;
static TelemetryRecord s_latest_telemetry;//最近的遥测记录
static ModbusRecord s_latest_modbus;
static volatile bool s_publish_latest;
static char s_status_topic[TOPIC_BUFFER_SIZE];
static char s_telemetry_topic[TOPIC_BUFFER_SIZE];
static char s_modbus_topic[TOPIC_BUFFER_SIZE];

static bool MqttConnected(void)
{
    return (xEventGroupGetBits(s_network_bits) & MQTT_CONNECTED_BIT) != 0U;
}

static void PublishJson(const char *topic, cJSON *root)
{
    char *json;//json指向可以通过MQTT发送的JSON文本字符串

    if (!MqttConnected() || root == NULL) return;
    json = cJSON_PrintUnformatted(root);
    if (json != NULL) {
        int message_id = esp_mqtt_client_publish(s_mqtt_client, topic, json, 0,
                                                  CONFIG_GATEWAY_MQTT_QOS, 0);//发送给broker
        ESP_LOGI(TAG, "MQTT TX topic=%s msg_id=%d payload=%s", topic, message_id, json);
        cJSON_free(json);
    }
}

static void PublishTelemetry(void)//esp32的MQTT客户端给Broker发送json
{
    cJSON *root;//内存中的JSON对象 不是最终字符串 root是指向内存中的JSON树

    if (!s_latest_telemetry.valid || !MqttConnected()) return;//已经收到过有效遥测并且MQTT已经连接Broker
    root = cJSON_CreateObject();
    if (root == NULL) return;

    cJSON_AddStringToObject(root, "device_id", CONFIG_GATEWAY_DEVICE_ID);
    cJSON_AddNumberToObject(root, "sequence", s_latest_telemetry.sequence);
    cJSON_AddBoolToObject(root, "aht20_online", s_latest_telemetry.aht20_ready);
    cJSON_AddNumberToObject(root, "temperature_c",
                           (double)s_latest_telemetry.temperature_deci_c / 10.0);
    cJSON_AddNumberToObject(root, "humidity_percent",
                           (double)s_latest_telemetry.humidity_deci_percent / 10.0);
    cJSON_AddBoolToObject(root, "ina219_online", s_latest_telemetry.ina219_ready);
    cJSON_AddNumberToObject(root, "bus_voltage_v",
                           (double)s_latest_telemetry.bus_voltage_mv / 1000.0);
    cJSON_AddNumberToObject(root, "current_ma", s_latest_telemetry.current_ma);
    cJSON_AddNumberToObject(root, "power_mw", s_latest_telemetry.power_mw);
    PublishJson(s_telemetry_topic, root);//前面是对root的处理 这里是发送
    cJSON_Delete(root);
}

static void PublishModbus(void)
{
    cJSON *root;

    if (!s_latest_modbus.valid || !MqttConnected()) return;
    root = cJSON_CreateObject();
    if (root == NULL) return;

    cJSON_AddStringToObject(root, "device_id", CONFIG_GATEWAY_DEVICE_ID);
    cJSON_AddNumberToObject(root, "sequence", s_latest_modbus.sequence);
    cJSON_AddBoolToObject(root, "online", s_latest_modbus.online);
    cJSON_AddNumberToObject(root, "slave", s_latest_modbus.slave_address);
    cJSON_AddNumberToObject(root, "exception", s_latest_modbus.exception_code);
    cJSON_AddNumberToObject(root, "register0", s_latest_modbus.register0);
    cJSON_AddNumberToObject(root, "register1", s_latest_modbus.register1);
    cJSON_AddNumberToObject(root, "success_count", (double)s_latest_modbus.success_count);
    cJSON_AddNumberToObject(root, "error_count", (double)s_latest_modbus.error_count);
    PublishJson(s_modbus_topic, root);
    cJSON_Delete(root);
}

static void PublishLatestRecords(void)
{
    if (!MqttConnected()) return;
    PublishTelemetry();
    PublishModbus();
    s_publish_latest = false;
}

static void SendAck(unsigned int sequence)//发送确认应答
{
    char ack[24];
    int length = snprintf(ack, sizeof(ack), "ESP,ACK,%u\r\n", sequence);

    if ((length > 0) && ((size_t)length < sizeof(ack))) {
        uart_write_bytes(GATEWAY_UART, ack, length);
        ESP_LOGI(TAG, "ACK TX: ESP,ACK,%u", sequence);
    }
}

static void ProcessTelemetry(const char *line)//处理遥测数据
{
    unsigned int aht20_ready;
    unsigned int ina219_ready;
    TelemetryRecord record = {0};//局部变量结构体

    if (sscanf(line, "GW,DATA,%u,%u,%d,%u,%u,%u,%d,%u",
               &record.sequence, &aht20_ready, &record.temperature_deci_c,
               &record.humidity_deci_percent, &ina219_ready,
               &record.bus_voltage_mv, &record.current_ma, &record.power_mw) != 8) {
        ESP_LOGW(TAG, "Invalid DATA frame: %s", line);
        return;
    }

    record.aht20_ready = aht20_ready != 0U;//aht20_ready != 0U 是关系表达式 成立返回1
    record.ina219_ready = ina219_ready != 0U;
    record.valid = true;
    s_latest_telemetry = record;//保存最近一份结构体
    ESP_LOGI(TAG, "DATA seq=%u AHT=%u T=%d H=%u INA=%u V=%u I=%d P=%u",
             record.sequence, aht20_ready, record.temperature_deci_c,
             record.humidity_deci_percent, ina219_ready, record.bus_voltage_mv,
             record.current_ma, record.power_mw);
    if (MqttConnected()) PublishTelemetry();//即使MQTT断开，ESP32也会保留最近一份STM32遥测数据
}

static void ProcessModbus(const char *line)
{
    unsigned int online;
    ModbusRecord record = {0};

    if (sscanf(line, "GW,MB,%u,%u,%u,%u,%u,%u,%lu,%lu",
               &record.sequence, &online, &record.slave_address,
               &record.exception_code, &record.register0, &record.register1,
               &record.success_count, &record.error_count) != 8) {
        ESP_LOGW(TAG, "Invalid MB frame: %s", line);
        return;
    }

    record.online = online != 0U;
    record.valid = true;
    s_latest_modbus = record;
    ESP_LOGI(TAG, "MODBUS seq=%u online=%u slave=%u r0=%u r1=%u ok=%lu err=%lu",
             record.sequence, online, record.slave_address, record.register0,
             record.register1, record.success_count, record.error_count);
    if (MqttConnected()) PublishModbus();
}

static void ProcessLine(char *line)//处理一行完整协议帧
{
    unsigned int sequence;

    if (sscanf(line, "GW,HB,%u", &sequence) == 1) {
        ESP_LOGI(TAG, "STM32 HB: seq=%u", sequence);
        SendAck(sequence);
    } else if (strncmp(line, "GW,DATA,", 8U) == 0) {
        ProcessTelemetry(line);
    } else if (strncmp(line, "GW,MB,", 6U) == 0) {
        ProcessModbus(line);
    } else if (line[0] != '\0') {
        ESP_LOGW(TAG, "Ignored frame: %s", line);
    }
}

static void GatewayUartTask(void *argument)//网关UART接收任务
{
    uint8_t data[RX_BUFFER_SIZE];
    char line[LINE_BUFFER_SIZE];
    size_t line_length = 0U;

    (void)argument;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) {
        int length = uart_read_bytes(GATEWAY_UART, data, sizeof(data), pdMS_TO_TICKS(200));

        for (int index = 0; index < length; index++) {
            uint8_t byte = data[index];
            if (byte == '\r') continue;
            if (byte == '\n') {
                line[line_length] = '\0';
                ProcessLine(line);
                line_length = 0U;
                continue;
            }
            if (line_length < (sizeof(line) - 1U)) {
                line[line_length++] = (char)byte;
            } else {
                line_length = 0U;
                ESP_LOGW(TAG, "UART line overflow; dropped");
            }
        }

        if (s_publish_latest) PublishLatestRecords();
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
}

static void WifiEventHandler(void *argument, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        esp_wifi_connect();//让 ESP32 STA 站点连接已提前配置好的 WiFi 路由器 AP
    } else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        xEventGroupClearBits(s_network_bits, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT);
        esp_wifi_connect();
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        xEventGroupSetBits(s_network_bits, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        if ((s_mqtt_client != NULL) && !s_mqtt_started) {
            esp_err_t result = esp_mqtt_client_start(s_mqtt_client);
            if (result == ESP_OK) {
                s_mqtt_started = true;
                ESP_LOGI(TAG, "MQTT client started after Wi-Fi obtained an IP address");
            } else {
                ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(result));
            }
        }
    }
}

static void MqttEventHandler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    if (event_id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(s_network_bits, MQTT_CONNECTED_BIT);
        esp_mqtt_client_publish(s_mqtt_client, s_status_topic, "online", 0, 1, 1);//向Broker的状态Topic发布保留的online消息。
        s_publish_latest = true;//设置s_publish_latest = true，要求任务随后补发最近一份遥测和Modbus数据。
        ESP_LOGI(TAG, "MQTT connected");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(s_network_bits, MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "MQTT disconnected");
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGE(TAG, "MQTT error");
    }
}

static void InitGatewayUart(void)
{
    const uart_config_t uart_config = {
        .baud_rate = GATEWAY_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(GATEWAY_UART, RX_BUFFER_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GATEWAY_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(GATEWAY_UART, GATEWAY_UART_TX_PIN,
                                 GATEWAY_UART_RX_PIN, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "Gateway UART ready: RX=GPIO16, TX=GPIO17, 115200 8N1");
}

static void InitWifi(void)
{
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {0};

    if (CONFIG_GATEWAY_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty; configure it with menuconfig");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &WifiEventHandler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &WifiEventHandler, NULL));
    strncpy((char *)wifi_config.sta.ssid, CONFIG_GATEWAY_WIFI_SSID,
            sizeof(wifi_config.sta.ssid) - 1U);
    strncpy((char *)wifi_config.sta.password, CONFIG_GATEWAY_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password) - 1U);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_LOGI(TAG, "Wi-Fi power save: MIN_MODEM");
}

static void InitMqtt(void)
{
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = CONFIG_GATEWAY_MQTT_BROKER_URI,
        .credentials.client_id = CONFIG_GATEWAY_DEVICE_ID,
        .credentials.username = CONFIG_GATEWAY_MQTT_USERNAME[0] != '\0' ?
                                CONFIG_GATEWAY_MQTT_USERNAME : NULL,
        .credentials.authentication.password = CONFIG_GATEWAY_MQTT_PASSWORD[0] != '\0' ?
                                                CONFIG_GATEWAY_MQTT_PASSWORD : NULL,
        .session.last_will.topic = s_status_topic,
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 0,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    if ((CONFIG_GATEWAY_WIFI_SSID[0] == '\0') ||
        (CONFIG_GATEWAY_MQTT_BROKER_URI[0] == '\0')) {
        ESP_LOGW(TAG, "MQTT disabled until Wi-Fi SSID and broker URI are configured");
        return;
    }

    s_mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                                   MqttEventHandler, NULL));
    ESP_LOGI(TAG, "MQTT client initialized; waiting for Wi-Fi IP address");
}

void app_main(void)
{
    esp_err_t nvs_result;

    ESP_LOGI(TAG, "Reset reason=%d", (int)esp_reset_reason());

    s_network_bits = xEventGroupCreate();
    if (s_network_bits == NULL) {
        ESP_LOGE(TAG, "Failed to create network event group");
        return;
    }

    snprintf(s_status_topic, sizeof(s_status_topic), "industrial_gateway/%s/status",
             CONFIG_GATEWAY_DEVICE_ID);
    snprintf(s_telemetry_topic, sizeof(s_telemetry_topic), "industrial_gateway/%s/telemetry",
             CONFIG_GATEWAY_DEVICE_ID);
    snprintf(s_modbus_topic, sizeof(s_modbus_topic), "industrial_gateway/%s/modbus",
             CONFIG_GATEWAY_DEVICE_ID);

    nvs_result = nvs_flash_init();
    if ((nvs_result == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_result);
    }

    InitGatewayUart();
    InitWifi();
    InitMqtt();
    if (xTaskCreate(GatewayUartTask, "gateway_uart", 4096, NULL, 10, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create gateway UART task");
    }
}
