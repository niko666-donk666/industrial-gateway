#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdbool.h>
#include <stdint.h>

#define MODBUS_MAX_REGISTERS 4U
#define MODBUS_MAX_RESPONSE_BYTES (5U + (MODBUS_MAX_REGISTERS * 2U))

typedef enum {
    MODBUS_ERROR_NONE = 0U,
    MODBUS_ERROR_ARGUMENT = 1U,
    MODBUS_ERROR_TRANSMIT = 2U,
    MODBUS_ERROR_HEADER_TIMEOUT = 3U,
    MODBUS_ERROR_HEADER = 4U,
    MODBUS_ERROR_EXCEPTION = 5U,
    MODBUS_ERROR_BYTE_COUNT = 6U,
    MODBUS_ERROR_PAYLOAD_TIMEOUT = 7U,
    MODBUS_ERROR_CRC = 8U,
    MODBUS_ERROR_UART = 9U
} ModbusError;

typedef struct {
    bool online;
    uint8_t slave_address;
    uint8_t exception_code;
    uint8_t register_count;
    uint8_t last_error;
    uint8_t received_bytes;
    uint8_t expected_bytes;
    uint8_t response_bytes[MODBUS_MAX_RESPONSE_BYTES];
    uint32_t uart_error;
    uint16_t registers[MODBUS_MAX_REGISTERS];
    uint32_t success_count;
    uint32_t error_count;
} ModbusData;

void Modbus_Init(void);
bool Modbus_ReadHoldingRegisters(uint8_t slave_address,
                                uint16_t start_register,
                                uint8_t register_count,
                                ModbusData *data);

#endif
