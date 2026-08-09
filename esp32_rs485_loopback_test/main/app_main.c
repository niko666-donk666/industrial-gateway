#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TEST_UART        UART_NUM_2
#define TEST_UART_TX_PIN GPIO_NUM_17
#define TEST_UART_RX_PIN GPIO_NUM_16
#define TEST_UART_BAUD   9600
#define RX_BUFFER_SIZE   128

static const char *TAG = "rs485_test";

void app_main(void)
{
    static const uint8_t expected_request[8] = {
        0x01U, 0x03U, 0x00U, 0x00U, 0x00U, 0x02U, 0xC4U, 0x0BU
    };
    static const uint8_t fixed_response[9] = {
        0x01U, 0x03U, 0x04U, 0x04U, 0xD2U, 0x16U, 0x2EU, 0xD5U, 0x46U
    };
    const uart_config_t uart_config = {
        .baud_rate = TEST_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uint8_t request[sizeof(expected_request)];
    size_t received = 0U;

    ESP_ERROR_CHECK(uart_driver_install(TEST_UART, RX_BUFFER_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(TEST_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(TEST_UART, TEST_UART_TX_PIN, TEST_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGW(TAG, "RS485 LOOPBACK TEST MODE ACTIVE");
    ESP_LOGI(TAG, "GPIO16 receives STM32 PA9 request at 9600 8N1");
    ESP_LOGI(TAG, "GPIO17 sends fixed Modbus response after 5 ms");

    for (;;) {
        int length = uart_read_bytes(TEST_UART,
                                     &request[received],
                                     sizeof(request) - received,
                                     pdMS_TO_TICKS(500));
        if (length <= 0) {
            if (received != 0U) {
                ESP_LOGW(TAG, "Partial request discarded: %u/8 bytes",
                         (unsigned int)received);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, request, received, ESP_LOG_WARN);
                received = 0U;
            }
            continue;
        }

        received += (size_t)length;
        if (received < sizeof(request)) continue;

        ESP_LOG_BUFFER_HEX_LEVEL(TAG, request, sizeof(request), ESP_LOG_INFO);
        if (memcmp(request, expected_request, sizeof(request)) == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            uart_write_bytes(TEST_UART, fixed_response, sizeof(fixed_response));
            ESP_ERROR_CHECK(uart_wait_tx_done(TEST_UART, pdMS_TO_TICKS(100)));
            ESP_LOGI(TAG, "TX: 01 03 04 04 D2 16 2E D5 46");
        } else {
            ESP_LOGW(TAG, "Unexpected request; response not sent");
        }
        received = 0U;
    }
}
