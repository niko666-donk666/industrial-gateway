#include "main.h"
#include "modbus_rtu.h"

#include <string.h>

#define MODBUS_FUNCTION_READ_HOLDING 0x03U
#define MODBUS_UART_TIMEOUT_MS       200U
#define MODBUS_INTER_FRAME_MS          5U

extern UART_HandleTypeDef huart1;

static uint16_t ModbusCrc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (crc >> 1) ^ 0xA001U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static bool HasValidCrc(const uint8_t *frame, uint16_t length)
{
    uint16_t received_crc;

    if (length < 4U) return false;
    received_crc = (uint16_t)frame[length - 2U] |
                   ((uint16_t)frame[length - 1U] << 8);
    return ModbusCrc16(frame, length - 2U) == received_crc;
}

static void SetTransmitMode(bool transmit)
{
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin,
                      transmit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void ClearReceiveState(void)
{
    volatile uint32_t discard;

    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET) {
        discard = huart1.Instance->DR;
    }

    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) != RESET) {
        discard = huart1.Instance->SR;
        discard = huart1.Instance->DR;
    }

    huart1.ErrorCode = HAL_UART_ERROR_NONE;
    huart1.RxState = HAL_UART_STATE_READY;
}

static HAL_StatusTypeDef ReceiveExact(uint8_t *buffer,
                                      uint8_t length,
                                      uint8_t *received,
                                      uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    while (*received < length) {
        uint32_t elapsed = HAL_GetTick() - start_tick;
        uint32_t remaining;
        HAL_StatusTypeDef status;

        if (elapsed >= timeout_ms) {
            return HAL_TIMEOUT;
        }
        remaining = timeout_ms - elapsed;
        status = HAL_UART_Receive(&huart1, &buffer[*received], 1U, remaining);
        if (status != HAL_OK) {
            return status;
        }
        (*received)++;
    }
    return HAL_OK;
}

static void SaveReceivedBytes(ModbusData *data, const uint8_t *response)
{
    uint8_t length = data->received_bytes;

    if (length > MODBUS_MAX_RESPONSE_BYTES) {
        length = MODBUS_MAX_RESPONSE_BYTES;
    }
    memset(data->response_bytes, 0, sizeof(data->response_bytes));
    if (length > 0U) {
        memcpy(data->response_bytes, response, length);
    }
}

void Modbus_Init(void)
{
    SetTransmitMode(false);
}

bool Modbus_ReadHoldingRegisters(uint8_t slave_address,
                                uint16_t start_register,
                                uint8_t register_count,
                                ModbusData *data)
{
    uint8_t request[8];
    uint8_t response[5U + (MODBUS_MAX_REGISTERS * 2U)];
    uint16_t crc;
    uint16_t response_length;
    uint8_t index;
    uint8_t received;
    HAL_StatusTypeDef receive_status;

    if ((data == NULL) || (slave_address == 0U) ||
        (register_count == 0U) || (register_count > MODBUS_MAX_REGISTERS)) {
        if (data != NULL) {
            data->last_error = MODBUS_ERROR_ARGUMENT;
            data->received_bytes = 0U;
        }
        return false;
    }

    request[0] = slave_address;
    request[1] = MODBUS_FUNCTION_READ_HOLDING;
    request[2] = (uint8_t)(start_register >> 8);
    request[3] = (uint8_t)start_register;
    request[4] = 0U;
    request[5] = register_count;
    crc = ModbusCrc16(request, 6U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8);

    data->slave_address = slave_address;
    data->register_count = register_count;
    data->exception_code = 0U;
    data->last_error = MODBUS_ERROR_NONE;
    data->received_bytes = 0U;
    data->expected_bytes = (uint8_t)(5U + (register_count * 2U));
    data->uart_error = HAL_UART_ERROR_NONE;
    memset(data->response_bytes, 0, sizeof(data->response_bytes));

    ClearReceiveState();
    SetTransmitMode(true);
    if (HAL_UART_Transmit(&huart1, request, sizeof(request), MODBUS_UART_TIMEOUT_MS) != HAL_OK) {
        SetTransmitMode(false);
        data->online = false;
        data->last_error = MODBUS_ERROR_TRANSMIT;
        data->uart_error = huart1.ErrorCode;
        data->error_count++;
        return false;
    }
    SetTransmitMode(false);

    memset(response, 0, sizeof(response));
    received = 0U;
    receive_status = ReceiveExact(response, 3U, &received, MODBUS_UART_TIMEOUT_MS);
    if (receive_status != HAL_OK) {
        data->received_bytes = received;
        data->uart_error = huart1.ErrorCode;
        SaveReceivedBytes(data, response);
        data->online = false;
        data->last_error = (receive_status == HAL_TIMEOUT) ?
                           MODBUS_ERROR_HEADER_TIMEOUT : MODBUS_ERROR_UART;
        data->error_count++;
        HAL_Delay(MODBUS_INTER_FRAME_MS);
        return false;
    }
    data->received_bytes = 3U;
    SaveReceivedBytes(data, response);

    if ((response[0] != slave_address) ||
        ((response[1] != MODBUS_FUNCTION_READ_HOLDING) &&
         (response[1] != (MODBUS_FUNCTION_READ_HOLDING | 0x80U)))) {
        data->online = false;
        data->last_error = MODBUS_ERROR_HEADER;
        data->error_count++;
        return false;
    }

    if ((response[1] & 0x80U) != 0U) {
        receive_status = ReceiveExact(response, 5U, &received, MODBUS_UART_TIMEOUT_MS);
        data->received_bytes = received;
        data->uart_error = huart1.ErrorCode;
        SaveReceivedBytes(data, response);
        if ((receive_status == HAL_OK) && HasValidCrc(response, 5U)) {
            data->exception_code = response[2];
        }
        data->online = false;
        data->last_error = MODBUS_ERROR_EXCEPTION;
        data->error_count++;
        return false;
    }

    if (response[2] != (uint8_t)(register_count * 2U)) {
        data->online = false;
        data->last_error = MODBUS_ERROR_BYTE_COUNT;
        data->error_count++;
        return false;
    }

    response_length = (uint16_t)(5U + (register_count * 2U));
    receive_status = ReceiveExact(response, (uint8_t)response_length, &received,
                                  MODBUS_UART_TIMEOUT_MS);
    if (receive_status != HAL_OK) {
        data->received_bytes = received;
        data->uart_error = huart1.ErrorCode;
        SaveReceivedBytes(data, response);
        data->online = false;
        data->last_error = (receive_status == HAL_TIMEOUT) ?
                           MODBUS_ERROR_PAYLOAD_TIMEOUT : MODBUS_ERROR_UART;
        data->error_count++;
        return false;
    }
    data->received_bytes = (uint8_t)response_length;
    SaveReceivedBytes(data, response);
    if (!HasValidCrc(response, response_length)) {
        data->online = false;
        data->last_error = MODBUS_ERROR_CRC;
        data->error_count++;
        return false;
    }

    for (index = 0U; index < register_count; index++) {
        data->registers[index] = ((uint16_t)response[3U + (index * 2U)] << 8) |
                                  response[4U + (index * 2U)];
    }
    data->online = true;
    data->last_error = MODBUS_ERROR_NONE;
    data->success_count++;
    return true;
}
