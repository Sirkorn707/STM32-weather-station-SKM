#ifndef APP_H
#define APP_H

#include "main.h"
#include <stdint.h>

typedef enum
{
    APP_TREND_STABLE = 0,
    APP_TREND_RISING,
    APP_TREND_FALLING
} APP_TrendTypeDef;

void APP_Init(I2C_HandleTypeDef *bmp_i2c, I2C_HandleTypeDef *lcd_i2c, UART_HandleTypeDef *cli_uart);
void APP_Process(void);

void APP_SetLed(uint8_t on);
void APP_SetMeasurement(uint8_t on);
void APP_SetLcdPower(uint8_t on);
uint8_t APP_SetRefreshRate(uint8_t rate);

uint8_t APP_GetMeasurementEnabled(void);
uint8_t APP_GetLcdEnabled(void);
uint8_t APP_GetRefreshRate(void);
uint32_t APP_GetRefreshPeriodMs(void);
void APP_GetLastValues(float *temperature_c, float *pressure_hpa, uint8_t *valid);

/* Min/max i trend */
void APP_GetMinMax(float *min_temp_c, float *max_temp_c, float *min_press_hpa, float *max_press_hpa);
void APP_ResetMinMax(void);
APP_TrendTypeDef APP_GetPressureTrend(void);

/* Logowanie CSV na UART (do przechwycenia np. przez skrypt na PC) */
void APP_SetLogging(uint8_t on);
uint8_t APP_GetLogging(void);

/* Diagnostyka czujnika BMP280 */
uint16_t APP_GetSensorFaultCount(void);

#endif
