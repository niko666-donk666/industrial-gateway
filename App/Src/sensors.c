#include "main.h"
#include "sensors.h"

#define AHT20_I2C_ADDR       (0x38U << 1)
#define INA219_I2C_ADDR      (0x40U << 1)
#define INA219_CONFIG_REG    0x00U
#define INA219_SHUNT_REG     0x01U
#define INA219_BUS_REG       0x02U
#define I2C_TIMEOUT_MS       100U
#define INA219_SHUNT_MILLIOHM 100

extern I2C_HandleTypeDef hi2c1;

static bool s_aht20_ready;
static bool s_ina219_ready;

static bool I2cRead(uint16_t address, uint8_t reg, uint8_t *data, uint16_t length)
{
    return HAL_I2C_Mem_Read(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT,
                            data, length, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool I2cWrite(uint16_t address, uint8_t reg, const uint8_t *data, uint16_t length)
{
    return HAL_I2C_Mem_Write(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)data, length, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool Aht20Read(int16_t *temperature_deci_c, uint16_t *humidity_deci_percent)
{
    uint8_t command[] = {0xACU, 0x33U, 0x00U};
    uint8_t data[6];
    uint32_t raw_humidity;
    uint32_t raw_temperature;

    if (HAL_I2C_Master_Transmit(&hi2c1, AHT20_I2C_ADDR, command, sizeof(command), I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }
    HAL_Delay(80U);
    if ((HAL_I2C_Master_Receive(&hi2c1, AHT20_I2C_ADDR, data, sizeof(data), I2C_TIMEOUT_MS) != HAL_OK) ||
        ((data[0] & 0x80U) != 0U)) {
        return false;
    }

    raw_humidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    raw_temperature = ((uint32_t)(data[3] & 0x0FU) << 16) | ((uint32_t)data[4] << 8) | data[5];
    *humidity_deci_percent = (uint16_t)((raw_humidity * 1000U + 524288U) / 1048576U);
    *temperature_deci_c = (int16_t)((((int32_t)raw_temperature * 2000L) + 524288L) / 1048576L - 500L);
    return true;
}

static bool Ina219Read(SensorData *data)
{
    uint8_t raw_bus[2];
    uint8_t raw_shunt_bytes[2];
    int16_t raw_shunt;
    int32_t shunt_uv;
    int32_t current_ma;

    if (!I2cRead(INA219_I2C_ADDR, INA219_BUS_REG, raw_bus, sizeof(raw_bus)) ||
        !I2cRead(INA219_I2C_ADDR, INA219_SHUNT_REG,
                 raw_shunt_bytes, sizeof(raw_shunt_bytes))) {
        return false;
    }

    data->bus_voltage_mv = (uint16_t)((((uint16_t)raw_bus[0] << 8 | raw_bus[1]) >> 3) * 4U);
    raw_shunt = (int16_t)(((uint16_t)raw_shunt_bytes[0] << 8) | raw_shunt_bytes[1]);
    shunt_uv = (int32_t)raw_shunt * 10L;
    current_ma = shunt_uv / INA219_SHUNT_MILLIOHM;
    data->current_ma = (int16_t)current_ma;
    data->power_mw = (uint16_t)(((uint32_t)data->bus_voltage_mv * (uint32_t)(current_ma < 0 ? -current_ma : current_ma)) / 1000U);
    return true;
}

void Sensors_Init(void)//HAL_I2C_IsDeviceReady() 检查指定的I2C地址有没有设备应答
{
    uint8_t status;
    const uint8_t aht20_init[] = {0xBEU, 0x08U, 0x00U};
    const uint8_t ina219_config[] = {0x39U, 0x9FU};

    s_aht20_ready = false;
    s_ina219_ready = false;
    if (HAL_I2C_IsDeviceReady(&hi2c1, AHT20_I2C_ADDR, 3U, I2C_TIMEOUT_MS) == HAL_OK) {
        if (HAL_I2C_Master_Receive(&hi2c1, AHT20_I2C_ADDR, &status, 1U, I2C_TIMEOUT_MS) == HAL_OK) {
            s_aht20_ready = (status & 0x08U) != 0U;
            if (!s_aht20_ready &&
                (HAL_I2C_Master_Transmit(&hi2c1, AHT20_I2C_ADDR, (uint8_t *)aht20_init,
                                         sizeof(aht20_init), I2C_TIMEOUT_MS) == HAL_OK)) {
                HAL_Delay(10U);
                if (HAL_I2C_Master_Receive(&hi2c1, AHT20_I2C_ADDR, &status, 1U, I2C_TIMEOUT_MS) == HAL_OK) {
                    s_aht20_ready = (status & 0x08U) != 0U;
                }
            }
        }
    }
    if (HAL_I2C_IsDeviceReady(&hi2c1, INA219_I2C_ADDR, 3U, I2C_TIMEOUT_MS) == HAL_OK) {
        s_ina219_ready = I2cWrite(INA219_I2C_ADDR, INA219_CONFIG_REG, ina219_config, sizeof(ina219_config));
    }
}

void Sensors_Read(SensorData *data)
{
    if (data == NULL) return;

    data->aht20_ready = s_aht20_ready;
    data->ina219_ready = s_ina219_ready;
    if (s_aht20_ready && !Aht20Read(&data->temperature_deci_c, &data->humidity_deci_percent)) {
        data->aht20_ready = false;
    }
    if (s_ina219_ready && !Ina219Read(data)) {
        data->ina219_ready = false;
    }
}
