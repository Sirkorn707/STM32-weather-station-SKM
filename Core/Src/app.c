#include "app.h"
#include "bmp280.h"
#include "lcd_i2c.h"
#include "cli.h"

#include <stdio.h>
#include <string.h>

static BMP280_HandleTypeDef bmp;
static LCD_HandleTypeDef lcd;

static uint8_t bmp_ok = 0;
static uint8_t lcd_ok = 0;

static uint8_t measurement_enabled = 1;
static uint8_t lcd_enabled = 1;
static uint8_t refresh_rate = 3; /* domyślnie 500 ms */

static const uint32_t refresh_period_ms[6] = {
    0,     /* brak RATE 0 */
    100,   /* RATE 1: najszybszy praktyczny okres dla LCD + I2C */
    250,   /* RATE 2 */
    500,   /* RATE 3 */
    1000,  /* RATE 4 */
    2000   /* RATE 5: jeden pomiar na dwie sekundy */
};

static uint32_t last_measure_tick = 0;

#define MEAS_AVG_WINDOW 5u

static float last_temperature_c = 0.0f;   // ostatnia wartość wyświetlana, już uśredniona
static float last_pressure_hpa = 0.0f;    // ostatnia wartość wyświetlana, już uśredniona
static uint8_t last_valid = 0;

static float temp_acc = 0.0f;
static float press_acc = 0.0f;
static uint8_t avg_count = 0;

/* Min/max */
static float min_temp_c = 0.0f;
static float max_temp_c = 0.0f;
static float min_press_hpa = 0.0f;
static float max_press_hpa = 0.0f;

/* Trend cisnienia - liczony miedzy kolejnymi usrednionymi probkami */
#define APP_TREND_THRESHOLD_HPA 0.3f
static float prev_pressure_for_trend = 0.0f;
static uint8_t have_prev_pressure = 0;
static APP_TrendTypeDef pressure_trend = APP_TREND_STABLE;

/* Logowanie CSV na UART */
static uint8_t logging_enabled = 0;

/* Diagnostyka czujnika BMP280 - wykrywanie "ciszy" na magistrali i auto-restart */
#define BMP_FAULT_TIMEOUT_MS 5000u
static I2C_HandleTypeDef *bmp_i2c_handle = NULL;
static uint32_t last_data_tick = 0;
static uint16_t sensor_fault_count = 0;

static int32_t app_round_x100(float value)
{
    if (value >= 0.0f)
    {
        return (int32_t)(value * 100.0f + 0.5f);
    }
    return (int32_t)(value * 100.0f - 0.5f);
}

static void app_format_x100(char *dst, size_t dst_size, const char *prefix, float value, const char *suffix)
{
    int32_t x100 = app_round_x100(value);
    char sign = (x100 < 0) ? '-' : ' ';
    uint32_t abs_x100 = (x100 < 0) ? (uint32_t)(-x100) : (uint32_t)x100;

    snprintf(dst, dst_size, "%s%c%lu.%02lu %s",
             prefix,
             sign,
             (unsigned long)(abs_x100 / 100u),
             (unsigned long)(abs_x100 % 100u),
             suffix);
}

static void app_lcd_show_text(const char *l1, const char *l2)
{
    if (!lcd_ok || !lcd_enabled)
    {
        return;
    }

    LCD_SetCursor(&lcd, 0, 0);
    LCD_PrintFixed(&lcd, l1, 20);
    LCD_SetCursor(&lcd, 0, 1);
    LCD_PrintFixed(&lcd, l2, 20);
}

static void app_lcd_update_measurement(void)
{
    char line1[21];
    char line2[21];

    if (!last_valid)
    {
        snprintf(line1, sizeof(line1), "Temp: --.-- C");
        snprintf(line2, sizeof(line2), "Cisn: ---.-- hPa");
    }
    else
    {
        app_format_x100(line1, sizeof(line1), "Temp:", last_temperature_c, "C");
        app_format_x100(line2, sizeof(line2), "Cisn:", last_pressure_hpa, "hPa");
    }

    app_lcd_show_text(line1, line2);
}

void APP_Init(I2C_HandleTypeDef *bmp_i2c, I2C_HandleTypeDef *lcd_i2c, UART_HandleTypeDef *cli_uart)
{
	bmp_i2c_handle = bmp_i2c;

	CLI_Init(cli_uart);

    CLI_Printf("\r\nBMP280 + LCD2004 + CLI start\r\n");
    CLI_Printf("Wpisz: help\r\n\r\n");

    if (LCD_Init(&lcd, lcd_i2c, 0, 20, 4) == HAL_OK)
    {
        lcd_ok = 1;
        CLI_Printf("LCD OK, adres I2C2: 0x%02X\r\n", lcd.address_7bit);
        app_lcd_show_text("BMP280 + LCD2004", "Inicjalizacja...");
    }
    else
    {
        lcd_ok = 0;
        CLI_Printf("LCD ERROR: nie znaleziono adresu na I2C2\r\n");
    }

    if (BMP280_Init(&bmp, bmp_i2c, BMP280_I2C_ADDR_7BIT) == HAL_OK)
    {
        bmp_ok = 1;
        CLI_Printf("BMP280 OK, adres I2C1: 0x%02X\r\n", BMP280_I2C_ADDR_7BIT);
    }
    else
    {
        bmp_ok = 0;
        CLI_Printf("BMP280 ERROR: sprawdz zasilanie, SDA/SCL i adres 0x76\r\n");
    }

    app_lcd_update_measurement();
    last_measure_tick = HAL_GetTick();
    last_data_tick = last_measure_tick;
    CLI_Printf("> ");
}

void APP_Process(void)
{
    CLI_Process();

    if (!bmp_ok)
    {
        return;
    }

    uint32_t now = HAL_GetTick();

    if (BMP280_HasNewData(&bmp))
    {
        float temp_now = 0.0f;
        float press_now = 0.0f;

        BMP280_GetLast(&bmp, &temp_now, &press_now);
        last_data_tick = now; /* czujnik zyje - dostalismy swieza probke */

        temp_acc += temp_now;
        press_acc += press_now;
        avg_count++;

        if (avg_count >= MEAS_AVG_WINDOW)
        {
            last_temperature_c = temp_acc / (float)MEAS_AVG_WINDOW;
            last_pressure_hpa = press_acc / (float)MEAS_AVG_WINDOW;

            if (!last_valid)
            {
                /* pierwszy poprawny pomiar - inicjalizujemy min/max */
                min_temp_c = max_temp_c = last_temperature_c;
                min_press_hpa = max_press_hpa = last_pressure_hpa;
            }
            else
            {
                if (last_temperature_c < min_temp_c) min_temp_c = last_temperature_c;
                if (last_temperature_c > max_temp_c) max_temp_c = last_temperature_c;
                if (last_pressure_hpa < min_press_hpa) min_press_hpa = last_pressure_hpa;
                if (last_pressure_hpa > max_press_hpa) max_press_hpa = last_pressure_hpa;
            }

            if (have_prev_pressure)
            {
                float diff = last_pressure_hpa - prev_pressure_for_trend;
                if (diff > APP_TREND_THRESHOLD_HPA)
                {
                    pressure_trend = APP_TREND_RISING;
                }
                else if (diff < -APP_TREND_THRESHOLD_HPA)
                {
                    pressure_trend = APP_TREND_FALLING;
                }
                else
                {
                    pressure_trend = APP_TREND_STABLE;
                }
            }
            prev_pressure_for_trend = last_pressure_hpa;
            have_prev_pressure = 1;

            last_valid = 1;

            temp_acc = 0.0f;
            press_acc = 0.0f;
            avg_count = 0;

            if (lcd_enabled)
            {
                app_lcd_update_measurement();
            }

            if (logging_enabled)
            {
                int32_t t100 = (int32_t)(last_temperature_c * 100.0f);
                int32_t p100 = (int32_t)(last_pressure_hpa * 100.0f);
                CLI_Printf("LOG,%lu,%ld,%ld\r\n",
                           (unsigned long)now, (long)t100, (long)p100);
            }
        }
    }
    else if (measurement_enabled && (now - last_data_tick) > BMP_FAULT_TIMEOUT_MS)
    {
        /* Brak nowych danych z czujnika przez dlugi czas - probujemy reinicjalizacji */
        sensor_fault_count++;
        CLI_Printf("\r\nBMP280: brak danych >%lu ms (usterka #%u), restart czujnika...\r\n",
                   (unsigned long)BMP_FAULT_TIMEOUT_MS, sensor_fault_count);

        if (bmp_i2c_handle != NULL && BMP280_Init(&bmp, bmp_i2c_handle, BMP280_I2C_ADDR_7BIT) == HAL_OK)
        {
            CLI_Printf("BMP280: restart OK\r\n> ");
            bmp_ok = 1;
        }
        else
        {
            CLI_Printf("BMP280: restart nieudany, sprawdz polaczenie\r\n> ");
            bmp_ok = 0;
        }
        last_data_tick = now;
    }

    if (!measurement_enabled)
    {
        return;
    }

    if ((now - last_measure_tick) >= APP_GetRefreshPeriodMs())
    {
        if (!BMP280_IsBusy(&bmp))
        {
            if (BMP280_ReadAsync(&bmp) == HAL_OK)
            {
                last_measure_tick = now;
            }
        }
    }
}

void APP_SetLed(uint8_t on)
{
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void APP_SetMeasurement(uint8_t on)
{
    measurement_enabled = on ? 1 : 0;

    if (measurement_enabled)
    {
        temp_acc = 0.0f;
        press_acc = 0.0f;
        avg_count = 0;
    }
}

void APP_SetLcdPower(uint8_t on)
{
    lcd_enabled = on ? 1 : 0;

    if (!lcd_ok)
    {
        return;
    }

    if (lcd_enabled)
    {
        LCD_Backlight(&lcd, 1);
        LCD_DisplayOn(&lcd);
        app_lcd_update_measurement();
    }
    else
    {
        /*
         * Wyłączamy samo wyświetlanie/podświetlenie.
         * Pomiary dalej mogą się wykonywać, a ostatnie wartości zostają w pamięci programu.
         */
        LCD_DisplayOff(&lcd);
        LCD_Backlight(&lcd, 0);
    }
}

uint8_t APP_SetRefreshRate(uint8_t rate)
{
    if (rate < 1 || rate > 5)
    {
        return 0;
    }

    refresh_rate = rate;
    return 1;
}

uint8_t APP_GetMeasurementEnabled(void)
{
    return measurement_enabled;
}

uint8_t APP_GetLcdEnabled(void)
{
    return lcd_enabled;
}

uint8_t APP_GetRefreshRate(void)
{
    return refresh_rate;
}

uint32_t APP_GetRefreshPeriodMs(void)
{
    return refresh_period_ms[refresh_rate];
}

void APP_GetLastValues(float *temperature_c, float *pressure_hpa, uint8_t *valid)
{
    if (temperature_c != NULL) *temperature_c = last_temperature_c;
    if (pressure_hpa != NULL) *pressure_hpa = last_pressure_hpa;
    if (valid != NULL) *valid = last_valid;
}

void APP_GetMinMax(float *min_temp_out, float *max_temp_out, float *min_press_out, float *max_press_out)
{
    if (min_temp_out != NULL)  *min_temp_out = min_temp_c;
    if (max_temp_out != NULL)  *max_temp_out = max_temp_c;
    if (min_press_out != NULL) *min_press_out = min_press_hpa;
    if (max_press_out != NULL) *max_press_out = max_press_hpa;
}

void APP_ResetMinMax(void)
{
    if (last_valid)
    {
        min_temp_c = max_temp_c = last_temperature_c;
        min_press_hpa = max_press_hpa = last_pressure_hpa;
    }
    else
    {
        min_temp_c = max_temp_c = 0.0f;
        min_press_hpa = max_press_hpa = 0.0f;
    }
}

APP_TrendTypeDef APP_GetPressureTrend(void)
{
    return pressure_trend;
}

void APP_SetLogging(uint8_t on)
{
    logging_enabled = on ? 1 : 0;
}

uint8_t APP_GetLogging(void)
{
    return logging_enabled;
}

uint16_t APP_GetSensorFaultCount(void)
{
    return sensor_fault_count;
}
