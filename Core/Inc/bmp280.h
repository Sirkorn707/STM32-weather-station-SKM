#ifndef BMP280_H
#define BMP280_H

#include "main.h"
#include <stdint.h>

#define BMP280_I2C_ADDR_7BIT      0x76u
#define BMP280_CHIP_ID_EXPECTED   0x58u

typedef struct
{
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} BMP280_CalibTypeDef;

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint8_t address_7bit;

    BMP280_CalibTypeDef calib;
    uint8_t rx_raw[6];

    volatile uint8_t busy;
    volatile uint8_t data_ready;

    int32_t t_fine;
    float temperature_c;
    float pressure_hpa;
} BMP280_HandleTypeDef;

HAL_StatusTypeDef BMP280_Init(BMP280_HandleTypeDef *dev, I2C_HandleTypeDef *hi2c, uint8_t address_7bit);
HAL_StatusTypeDef BMP280_ReadAsync(BMP280_HandleTypeDef *dev);
uint8_t BMP280_IsBusy(BMP280_HandleTypeDef *dev);
uint8_t BMP280_HasNewData(BMP280_HandleTypeDef *dev);
void BMP280_GetLast(BMP280_HandleTypeDef *dev, float *temperature_c, float *pressure_hpa);

void BMP280_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c);
void BMP280_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c);

#endif
