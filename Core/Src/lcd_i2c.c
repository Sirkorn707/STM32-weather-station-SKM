#include "lcd_i2c.h"
#include <string.h>

#define LCD_RS        0x01u
#define LCD_RW        0x02u
#define LCD_EN        0x04u
#define LCD_BL        0x08u

#define LCD_CMD_CLEAR      0x01u
#define LCD_CMD_HOME       0x02u
#define LCD_CMD_ENTRY      0x06u
#define LCD_CMD_DISPLAY_ON 0x0Cu
#define LCD_CMD_DISPLAY_OFF 0x08u
#define LCD_CMD_FUNCTION   0x28u
#define LCD_CMD_SET_DDRAM  0x80u

static volatile uint8_t lcd_dma_done = 1;
static I2C_HandleTypeDef *lcd_dma_hi2c = NULL;

static HAL_StatusTypeDef lcd_expander_write(LCD_HandleTypeDef *lcd, const uint8_t *data, uint16_t len)
{
    if (lcd == NULL || lcd->hi2c == NULL || data == NULL)
    {
        return HAL_ERROR;
    }

#if LCD_USE_DMA
    if (lcd->hi2c->hdmatx != NULL)
    {
        uint32_t start = HAL_GetTick();
        lcd_dma_done = 0;
        lcd_dma_hi2c = lcd->hi2c;

        HAL_StatusTypeDef st = HAL_I2C_Master_Transmit_DMA(lcd->hi2c, lcd->address_7bit << 1,
                                                           (uint8_t *)data, len);
        if (st != HAL_OK)
        {
            lcd_dma_done = 1;
            return st;
        }

        while (!lcd_dma_done)
        {
            if ((HAL_GetTick() - start) > 20)
            {
                return HAL_TIMEOUT;
            }
        }
        return HAL_OK;
    }
#endif

    return HAL_I2C_Master_Transmit(lcd->hi2c, lcd->address_7bit << 1, (uint8_t *)data, len, 20);
}

static HAL_StatusTypeDef lcd_send_nibble(LCD_HandleTypeDef *lcd, uint8_t nibble, uint8_t rs)
{
    uint8_t bl = lcd->backlight ? LCD_BL : 0;
    uint8_t data = (nibble & 0xF0u) | bl | (rs ? LCD_RS : 0);
    uint8_t seq[2] = { (uint8_t)(data | LCD_EN), data };

    HAL_StatusTypeDef st = lcd_expander_write(lcd, seq, sizeof(seq));
    for (volatile uint32_t i = 0; i < 250; i++) { __NOP(); }
    return st;
}

static HAL_StatusTypeDef lcd_send_byte(LCD_HandleTypeDef *lcd, uint8_t value, uint8_t rs)
{
    HAL_StatusTypeDef st;
    st = lcd_send_nibble(lcd, value & 0xF0u, rs);
    if (st != HAL_OK) return st;
    st = lcd_send_nibble(lcd, (uint8_t)((value << 4) & 0xF0u), rs);
    if (value == LCD_CMD_CLEAR || value == LCD_CMD_HOME)
    {
        HAL_Delay(2);
    }
    return st;
}

static HAL_StatusTypeDef lcd_command(LCD_HandleTypeDef *lcd, uint8_t cmd)
{
    return lcd_send_byte(lcd, cmd, 0);
}

static HAL_StatusTypeDef lcd_data(LCD_HandleTypeDef *lcd, uint8_t data)
{
    return lcd_send_byte(lcd, data, 1);
}

HAL_StatusTypeDef LCD_ScanAddress(I2C_HandleTypeDef *hi2c, uint8_t *address_7bit)
{
    if (hi2c == NULL || address_7bit == NULL)
    {
        return HAL_ERROR;
    }

    /* Najczęstsze adresy PCF8574/PCF8574A: 0x20-0x27 oraz 0x38-0x3F.
       Wiele modułów LCD ma 0x27 lub 0x3F. */
    for (uint8_t addr = 0x20; addr <= 0x27; addr++)
    {
        if (HAL_I2C_IsDeviceReady(hi2c, addr << 1, 2, 10) == HAL_OK)
        {
            *address_7bit = addr;
            return HAL_OK;
        }
    }

    for (uint8_t addr = 0x38; addr <= 0x3F; addr++)
    {
        if (HAL_I2C_IsDeviceReady(hi2c, addr << 1, 2, 10) == HAL_OK)
        {
            *address_7bit = addr;
            return HAL_OK;
        }
    }

    return HAL_ERROR;
}

HAL_StatusTypeDef LCD_Init(LCD_HandleTypeDef *lcd, I2C_HandleTypeDef *hi2c, uint8_t address_7bit_or_zero, uint8_t cols, uint8_t rows)
{
    if (lcd == NULL || hi2c == NULL)
    {
        return HAL_ERROR;
    }

    memset(lcd, 0, sizeof(*lcd));
    lcd->hi2c = hi2c;
    lcd->cols = cols;
    lcd->rows = rows;
    lcd->backlight = 1;

    if (address_7bit_or_zero == 0)
    {
        HAL_StatusTypeDef st = LCD_ScanAddress(hi2c, &lcd->address_7bit);
        if (st != HAL_OK)
        {
            return st;
        }
    }
    else
    {
        lcd->address_7bit = address_7bit_or_zero;
    }

    HAL_Delay(50);

    /* Sekwencja inicjalizacji HD44780 w trybie 4-bit przez PCF8574 */
    lcd_send_nibble(lcd, 0x30, 0);
    HAL_Delay(5);
    lcd_send_nibble(lcd, 0x30, 0);
    HAL_Delay(5);
    lcd_send_nibble(lcd, 0x30, 0);
    HAL_Delay(1);
    lcd_send_nibble(lcd, 0x20, 0);
    HAL_Delay(1);

    HAL_StatusTypeDef st;
    st = lcd_command(lcd, LCD_CMD_FUNCTION);
    if (st != HAL_OK) return st;
    st = lcd_command(lcd, LCD_CMD_DISPLAY_OFF);
    if (st != HAL_OK) return st;
    st = LCD_Clear(lcd);
    if (st != HAL_OK) return st;
    st = lcd_command(lcd, LCD_CMD_ENTRY);
    if (st != HAL_OK) return st;
    st = LCD_DisplayOn(lcd);

    lcd->initialized = (st == HAL_OK);
    return st;
}

HAL_StatusTypeDef LCD_Clear(LCD_HandleTypeDef *lcd)
{
    return lcd_command(lcd, LCD_CMD_CLEAR);
}

HAL_StatusTypeDef LCD_SetCursor(LCD_HandleTypeDef *lcd, uint8_t col, uint8_t row)
{
    static const uint8_t row_offsets_20x4[] = {0x00, 0x40, 0x14, 0x54};

    if (lcd == NULL || row >= lcd->rows || col >= lcd->cols)
    {
        return HAL_ERROR;
    }

    return lcd_command(lcd, (uint8_t)(LCD_CMD_SET_DDRAM | (row_offsets_20x4[row] + col)));
}

HAL_StatusTypeDef LCD_Print(LCD_HandleTypeDef *lcd, const char *text)
{
    if (lcd == NULL || text == NULL)
    {
        return HAL_ERROR;
    }

    while (*text)
    {
        HAL_StatusTypeDef st = lcd_data(lcd, (uint8_t)*text++);
        if (st != HAL_OK)
        {
            return st;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef LCD_PrintFixed(LCD_HandleTypeDef *lcd, const char *text, uint8_t width)
{
    uint8_t i = 0;

    while (text != NULL && text[i] && i < width)
    {
        HAL_StatusTypeDef st = lcd_data(lcd, (uint8_t)text[i]);
        if (st != HAL_OK) return st;
        i++;
    }

    while (i < width)
    {
        HAL_StatusTypeDef st = lcd_data(lcd, ' ');
        if (st != HAL_OK) return st;
        i++;
    }

    return HAL_OK;
}

HAL_StatusTypeDef LCD_DisplayOn(LCD_HandleTypeDef *lcd)
{
    return lcd_command(lcd, LCD_CMD_DISPLAY_ON);
}

HAL_StatusTypeDef LCD_DisplayOff(LCD_HandleTypeDef *lcd)
{
    return lcd_command(lcd, LCD_CMD_DISPLAY_OFF);
}

HAL_StatusTypeDef LCD_Backlight(LCD_HandleTypeDef *lcd, uint8_t on)
{
    if (lcd == NULL)
    {
        return HAL_ERROR;
    }

    lcd->backlight = on ? 1 : 0;
    uint8_t val = lcd->backlight ? LCD_BL : 0;
    return lcd_expander_write(lcd, &val, 1);
}

void LCD_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == lcd_dma_hi2c)
    {
        lcd_dma_done = 1;
    }
}

void LCD_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == lcd_dma_hi2c)
    {
        lcd_dma_done = 1;
    }
}
