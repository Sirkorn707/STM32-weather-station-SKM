#ifndef LCD_I2C_H
#define LCD_I2C_H

#include "main.h"
#include <stdint.h>

#define LCD_USE_DMA  0

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint8_t address_7bit;
    uint8_t cols;
    uint8_t rows;
    uint8_t backlight;
    uint8_t initialized;
} LCD_HandleTypeDef;

HAL_StatusTypeDef LCD_ScanAddress(I2C_HandleTypeDef *hi2c, uint8_t *address_7bit);
HAL_StatusTypeDef LCD_Init(LCD_HandleTypeDef *lcd, I2C_HandleTypeDef *hi2c, uint8_t address_7bit_or_zero, uint8_t cols, uint8_t rows);

HAL_StatusTypeDef LCD_Clear(LCD_HandleTypeDef *lcd);
HAL_StatusTypeDef LCD_SetCursor(LCD_HandleTypeDef *lcd, uint8_t col, uint8_t row);
HAL_StatusTypeDef LCD_Print(LCD_HandleTypeDef *lcd, const char *text);
HAL_StatusTypeDef LCD_PrintFixed(LCD_HandleTypeDef *lcd, const char *text, uint8_t width);
HAL_StatusTypeDef LCD_DisplayOn(LCD_HandleTypeDef *lcd);
HAL_StatusTypeDef LCD_DisplayOff(LCD_HandleTypeDef *lcd);
HAL_StatusTypeDef LCD_Backlight(LCD_HandleTypeDef *lcd, uint8_t on);

void LCD_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c);
void LCD_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c);

#endif
