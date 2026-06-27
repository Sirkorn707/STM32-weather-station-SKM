#include "bmp280.h"
#include <string.h>

#define BMP280_REG_ID          0xD0u
#define BMP280_REG_RESET       0xE0u
#define BMP280_REG_CTRL_MEAS   0xF4u
#define BMP280_REG_CONFIG      0xF5u
#define BMP280_REG_PRESS_MSB   0xF7u
#define BMP280_REG_CALIB       0x88u

#define BMP280_RESET_VALUE     0xB6u

/* osrs_t=x1, osrs_p=x1, mode=normal */
#define BMP280_CTRL_NORMAL_X1  ((1u << 5) | (1u << 2) | 3u)
/* standby 125 ms, IIR filter off, SPI 3-wire off */
#define BMP280_CONFIG_VALUE    (2u << 5)

static BMP280_HandleTypeDef *active_dev = NULL;

static uint16_t u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t s16_le(const uint8_t *p)
{
    return (int16_t)u16_le(p);
}

static void bmp280_parse_calib(BMP280_HandleTypeDef *dev, const uint8_t *b)
{
    dev->calib.dig_T1 = u16_le(&b[0]);
    dev->calib.dig_T2 = s16_le(&b[2]);
    dev->calib.dig_T3 = s16_le(&b[4]);

    dev->calib.dig_P1 = u16_le(&b[6]);
    dev->calib.dig_P2 = s16_le(&b[8]);
    dev->calib.dig_P3 = s16_le(&b[10]);
    dev->calib.dig_P4 = s16_le(&b[12]);
    dev->calib.dig_P5 = s16_le(&b[14]);
    dev->calib.dig_P6 = s16_le(&b[16]);
    dev->calib.dig_P7 = s16_le(&b[18]);
    dev->calib.dig_P8 = s16_le(&b[20]);
    dev->calib.dig_P9 = s16_le(&b[22]);
}

static void bmp280_compensate(BMP280_HandleTypeDef *dev)
{
    int32_t adc_P = ((int32_t)dev->rx_raw[0] << 12) |
                    ((int32_t)dev->rx_raw[1] << 4)  |
                    ((int32_t)dev->rx_raw[2] >> 4);

    int32_t adc_T = ((int32_t)dev->rx_raw[3] << 12) |
                    ((int32_t)dev->rx_raw[4] << 4)  |
                    ((int32_t)dev->rx_raw[5] >> 4);

    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)dev->calib.dig_T1 << 1))) *
            ((int32_t)dev->calib.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dev->calib.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)dev->calib.dig_T1))) >> 12) *
            ((int32_t)dev->calib.dig_T3)) >> 14;

    dev->t_fine = var1 + var2;
    int32_t T = (dev->t_fine * 5 + 128) >> 8;
    dev->temperature_c = (float)T / 100.0f;

    int64_t p_var1, p_var2, p;
    p_var1 = ((int64_t)dev->t_fine) - 128000;
    p_var2 = p_var1 * p_var1 * (int64_t)dev->calib.dig_P6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)dev->calib.dig_P5) << 17);
    p_var2 = p_var2 + (((int64_t)dev->calib.dig_P4) << 35);
    p_var1 = ((p_var1 * p_var1 * (int64_t)dev->calib.dig_P3) >> 8) +
             ((p_var1 * (int64_t)dev->calib.dig_P2) << 12);
    p_var1 = (((((int64_t)1) << 47) + p_var1)) * ((int64_t)dev->calib.dig_P1) >> 33;

    if (p_var1 == 0)
    {
        dev->pressure_hpa = 0.0f;
        return;
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - p_var2) * 3125) / p_var1;
    p_var1 = (((int64_t)dev->calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    p_var2 = (((int64_t)dev->calib.dig_P8) * p) >> 19;
    p = ((p + p_var1 + p_var2) >> 8) + (((int64_t)dev->calib.dig_P7) << 4);

    float pressure_pa = (float)p / 256.0f;
    dev->pressure_hpa = pressure_pa / 100.0f;
}

HAL_StatusTypeDef BMP280_Init(BMP280_HandleTypeDef *dev, I2C_HandleTypeDef *hi2c, uint8_t address_7bit)
{
    if (dev == NULL || hi2c == NULL)
    {
        return HAL_ERROR;
    }

    memset(dev, 0, sizeof(*dev));
    dev->hi2c = hi2c;
    dev->address_7bit = address_7bit;
    active_dev = dev;

    uint8_t id = 0;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(hi2c, address_7bit << 1, BMP280_REG_ID,
                                            I2C_MEMADD_SIZE_8BIT, &id, 1, 100);
    if (st != HAL_OK || id != BMP280_CHIP_ID_EXPECTED)
    {
        return HAL_ERROR;
    }

    uint8_t reset = BMP280_RESET_VALUE;
    HAL_I2C_Mem_Write(hi2c, address_7bit << 1, BMP280_REG_RESET,
                      I2C_MEMADD_SIZE_8BIT, &reset, 1, 100);
    HAL_Delay(5);

    uint8_t calib[24];
    st = HAL_I2C_Mem_Read(hi2c, address_7bit << 1, BMP280_REG_CALIB,
                          I2C_MEMADD_SIZE_8BIT, calib, sizeof(calib), 200);
    if (st != HAL_OK)
    {
        return st;
    }
    bmp280_parse_calib(dev, calib);

    uint8_t config = BMP280_CONFIG_VALUE;
    st = HAL_I2C_Mem_Write(hi2c, address_7bit << 1, BMP280_REG_CONFIG,
                           I2C_MEMADD_SIZE_8BIT, &config, 1, 100);
    if (st != HAL_OK)
    {
        return st;
    }

    uint8_t ctrl = BMP280_CTRL_NORMAL_X1;
    st = HAL_I2C_Mem_Write(hi2c, address_7bit << 1, BMP280_REG_CTRL_MEAS,
                           I2C_MEMADD_SIZE_8BIT, &ctrl, 1, 100);

    return st;
}

HAL_StatusTypeDef BMP280_ReadAsync(BMP280_HandleTypeDef *dev)
{
    if (dev == NULL || dev->hi2c == NULL)
    {
        return HAL_ERROR;
    }

    if (dev->busy)
    {
        return HAL_BUSY;
    }

    dev->busy = 1;
    dev->data_ready = 0;

    /*
     * Jeżeli w CubeMX skonfigurowano DMA dla I2C1_RX, HAL użyje DMA.
     * Jeżeli DMA nie istnieje, kod automatycznie przechodzi na odczyt blokujący.
     */
    if (dev->hi2c->hdmarx != NULL)
    {
        HAL_StatusTypeDef st = HAL_I2C_Mem_Read_DMA(dev->hi2c, dev->address_7bit << 1,
                                                    BMP280_REG_PRESS_MSB,
                                                    I2C_MEMADD_SIZE_8BIT,
                                                    dev->rx_raw, sizeof(dev->rx_raw));
        if (st != HAL_OK)
        {
            dev->busy = 0;
        }
        return st;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(dev->hi2c, dev->address_7bit << 1,
                                            BMP280_REG_PRESS_MSB,
                                            I2C_MEMADD_SIZE_8BIT,
                                            dev->rx_raw, sizeof(dev->rx_raw), 100);
    if (st == HAL_OK)
    {
        bmp280_compensate(dev);
        dev->data_ready = 1;
    }
    dev->busy = 0;
    return st;
}

uint8_t BMP280_IsBusy(BMP280_HandleTypeDef *dev)
{
    return (dev != NULL) ? dev->busy : 0;
}

uint8_t BMP280_HasNewData(BMP280_HandleTypeDef *dev)
{
    return (dev != NULL) ? dev->data_ready : 0;
}

void BMP280_GetLast(BMP280_HandleTypeDef *dev, float *temperature_c, float *pressure_hpa)
{
    if (dev == NULL)
    {
        return;
    }

    if (temperature_c != NULL)
    {
        *temperature_c = dev->temperature_c;
    }
    if (pressure_hpa != NULL)
    {
        *pressure_hpa = dev->pressure_hpa;
    }
    dev->data_ready = 0;
}

void BMP280_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (active_dev != NULL && hi2c == active_dev->hi2c && active_dev->busy)
    {
        bmp280_compensate(active_dev);
        active_dev->data_ready = 1;
        active_dev->busy = 0;
    }
}

void BMP280_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (active_dev != NULL && hi2c == active_dev->hi2c)
    {
        active_dev->busy = 0;
    }
}
