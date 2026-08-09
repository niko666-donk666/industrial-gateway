#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool aht20_ready;//AHT20是否正常
    bool ina219_ready;//INA219是否正常
    int16_t temperature_deci_c;//0.1℃为单位的温度
    uint16_t humidity_deci_percent;//0.1%为单位的湿度
    uint16_t bus_voltage_mv;//母线电压，单位mV
    int16_t current_ma;//电流 mA
    uint16_t power_mw;//功率 mW
} SensorData;//完整传感器数据

void Sensors_Init(void);
void Sensors_Read(SensorData *data);

#endif
