#include "cli.h"
#include "app.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>

#define CLI_RX_BUF_SIZE 96
#define CLI_TX_BUF_SIZE 256
#define CLI_HISTORY_SIZE 8

static UART_HandleTypeDef *cli_huart = NULL;
static uint8_t rx_byte = 0;

static volatile uint8_t cmd_ready = 0;
static volatile uint16_t rx_pos = 0;
static char rx_buf[CLI_RX_BUF_SIZE];
static char cmd_buf[CLI_RX_BUF_SIZE];

static uint8_t cli_esc_state = 0;
static char cli_history[CLI_HISTORY_SIZE][CLI_RX_BUF_SIZE];
static uint8_t cli_history_count = 0;
static uint8_t cli_history_head = 0;
static int16_t cli_history_nav = -1;

static void cli_start_rx(void)
{
    if (cli_huart != NULL)
    {
        HAL_UART_Receive_IT(cli_huart, &rx_byte, 1);
    }
}

void CLI_Printf(const char *fmt, ...)
{
    if (cli_huart == NULL || fmt == NULL)
    {
        return;
    }

    char out[CLI_TX_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(out, sizeof(out), fmt, args);
    va_end(args);

    if (len <= 0)
    {
        return;
    }

    if (len > (int)sizeof(out))
    {
        len = sizeof(out);
    }

    HAL_UART_Transmit(cli_huart, (uint8_t *)out, (uint16_t)len, 200);
}

static void cli_trim(char *s)
{
    if (s == NULL) return;

    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;

    if (start != s)
    {
        memmove(s, start, strlen(start) + 1);
    }

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
    {
        s[len - 1] = '\0';
        len--;
    }
}

static uint8_t cli_equals(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static void cli_history_push(const char *cmd)
{
    if (cmd[0] == '\0')
    {
        return;
    }

    if (cli_history_count > 0)
    {
        uint8_t last_idx = (uint8_t)((cli_history_head + CLI_HISTORY_SIZE - 1) % CLI_HISTORY_SIZE);
        if (cli_equals(cli_history[last_idx], cmd))
        {
            return;
        }
    }

    strncpy(cli_history[cli_history_head], cmd, CLI_RX_BUF_SIZE - 1);
    cli_history[cli_history_head][CLI_RX_BUF_SIZE - 1] = '\0';

    cli_history_head = (uint8_t)((cli_history_head + 1) % CLI_HISTORY_SIZE);
    if (cli_history_count < CLI_HISTORY_SIZE)
    {
        cli_history_count++;
    }
}

static const char *cli_history_get(uint8_t nav)
{
    uint8_t idx = (uint8_t)((cli_history_head + CLI_HISTORY_SIZE - 1 - nav) % CLI_HISTORY_SIZE);
    return cli_history[idx];
}

static void cli_history_redraw(const char *text)
{
    while (rx_pos > 0)
    {
        rx_pos--;
        HAL_UART_Transmit(cli_huart, (uint8_t *)"\b \b", 3, 50);
    }

    size_t len = strlen(text);
    if (len > (CLI_RX_BUF_SIZE - 1))
    {
        len = CLI_RX_BUF_SIZE - 1;
    }

    memcpy(rx_buf, text, len);
    rx_buf[len] = '\0';
    rx_pos = (uint16_t)len;

    if (len > 0)
    {
        HAL_UART_Transmit(cli_huart, (uint8_t *)rx_buf, (uint16_t)len, 100);
    }
}

static void cli_history_recall(int8_t direction)
{
    if (direction > 0)
    {
        if ((cli_history_nav + 1) < (int16_t)cli_history_count)
        {
            cli_history_nav++;
            cli_history_redraw(cli_history_get((uint8_t)cli_history_nav));
        }
    }
    else
    {
        if (cli_history_nav > 0)
        {
            cli_history_nav--;
            cli_history_redraw(cli_history_get((uint8_t)cli_history_nav));
        }
        else if (cli_history_nav == 0)
        {
            cli_history_nav = -1;
            cli_history_redraw("");
        }
    }
}

static void cli_print_signed_x100(const char *label, float value, const char *suffix)
{
    int32_t x100 = (value >= 0.0f) ? (int32_t)(value * 100.0f + 0.5f) : (int32_t)(value * 100.0f - 0.5f);
    char sign = (x100 < 0) ? '-' : '+';
    uint32_t abs_x100 = (x100 < 0) ? (uint32_t)(-x100) : (uint32_t)x100;

    CLI_Printf("%s%c%lu.%02lu %s\r\n", label, sign,
               (unsigned long)(abs_x100 / 100u), (unsigned long)(abs_x100 % 100u), suffix);
}

static void cli_stats(void)
{
    float min_t = 0.0f, max_t = 0.0f, min_p = 0.0f, max_p = 0.0f;
    APP_GetMinMax(&min_t, &max_t, &min_p, &max_p);

    const char *trend_txt = "stabilnie";
    switch (APP_GetPressureTrend())
    {
        case APP_TREND_RISING:  trend_txt = "rosnie";  break;
        case APP_TREND_FALLING: trend_txt = "spada";   break;
        default:                trend_txt = "stabilnie"; break;
    }

    cli_print_signed_x100("min temp:    ", min_t, "C");
    cli_print_signed_x100("max temp:    ", max_t, "C");
    cli_print_signed_x100("min cisn:    ", min_p, "hPa");
    cli_print_signed_x100("max cisn:    ", max_p, "hPa");
    CLI_Printf("trend cisn.: %s\r\n", trend_txt);
    CLI_Printf("usterki bmp: %u\r\n", APP_GetSensorFaultCount());
}


static void cli_help(void)
{
    CLI_Printf("Dostepne komendy:\r\n");
    CLI_Printf("  help            - pokazuje liste komend\r\n");
    CLI_Printf("  led on          - wlacza diode LD2 na Nucleo-F429ZI\r\n");
    CLI_Printf("  led off         - wylacza diode LD2\r\n");
    CLI_Printf("  start meas      - rozpoczyna pomiary i aktualizacje LCD\r\n");
    CLI_Printf("  stop meas       - zatrzymuje pomiary; LCD pokazuje ostatnia wartosc\r\n");
    CLI_Printf("  lcd on          - wlacza LCD bez zatrzymywania pomiarow\r\n");
    CLI_Printf("  lcd off         - wylacza LCD bez zatrzymywania pomiarow\r\n");
    CLI_Printf("  refresh [1..5]  - ustawia okres pomiaru: 1=100 ms, 2=250 ms, 3=500 ms, 4=1 s, 5=2 s\r\n");
    CLI_Printf("  status          - pokazuje stan programu i ostatni pomiar\r\n");
    CLI_Printf("  stats           - pokazuje min/max temp./cisn. oraz trend cisnienia\r\n");
    CLI_Printf("  stats reset     - resetuje zapisane min/max\r\n");
    CLI_Printf("  log on          - wlacza ciagle wypisywanie CSV (LOG,tick,tempx100,presx100)\r\n");
    CLI_Printf("  log off         - wylacza wypisywanie CSV\r\n");
    CLI_Printf("  clear           - czysci ekran terminala\r\n");
    CLI_Printf("  (strzalka gora/dol - przegladanie historii komend)\r\n");
    }

static void cli_status(void)
{
    float t = 0.0f, p = 0.0f;
    uint8_t valid = 0;
    APP_GetLastValues(&t, &p, &valid);

    CLI_Printf("measurement: %s\r\n", APP_GetMeasurementEnabled() ? "ON" : "OFF");
    CLI_Printf("lcd:         %s\r\n", APP_GetLcdEnabled() ? "ON" : "OFF");
    CLI_Printf("refresh:     RATE=%u, okres=%lu ms\r\n",
               APP_GetRefreshRate(), (unsigned long)APP_GetRefreshPeriodMs());

    if (valid)
    {
        int32_t t100 = (t >= 0.0f) ? (int32_t)(t * 100.0f + 0.5f) : (int32_t)(t * 100.0f - 0.5f);
        int32_t p100 = (p >= 0.0f) ? (int32_t)(p * 100.0f + 0.5f) : (int32_t)(p * 100.0f - 0.5f);

        char ts = (t100 < 0) ? '-' : '+';
        uint32_t at = (t100 < 0) ? (uint32_t)(-t100) : (uint32_t)t100;
        uint32_t ap = (p100 < 0) ? (uint32_t)(-p100) : (uint32_t)p100;

        CLI_Printf("last:        %c%lu.%02lu C, %lu.%02lu hPa\r\n",
                   ts,
                   (unsigned long)(at / 100u),
                   (unsigned long)(at % 100u),
                   (unsigned long)(ap / 100u),
                   (unsigned long)(ap % 100u));
    }
    else
    {
        CLI_Printf("last:        brak poprawnego pomiaru\r\n");
    }
}

static void cli_execute(char *cmd)
{
    cli_trim(cmd);

    for (char *p = cmd; *p; ++p)
    {
        *p = (char)tolower((unsigned char)*p);
    }

    if (cmd[0] == '\0')
    {
        return;
    }

    if (cli_equals(cmd, "help"))
    {
        cli_help();
    }
    else if (cli_equals(cmd, "led on"))
    {
        APP_SetLed(1);
        CLI_Printf("LED LD2: ON\r\n");
    }
    else if (cli_equals(cmd, "led off"))
    {
        APP_SetLed(0);
        CLI_Printf("LED LD2: OFF\r\n");
    }
    else if (cli_equals(cmd, "start meas"))
    {
        APP_SetMeasurement(1);
        CLI_Printf("Pomiary: START\r\n");
    }
    else if (cli_equals(cmd, "stop meas"))
    {
        APP_SetMeasurement(0);
        CLI_Printf("Pomiary: STOP; LCD zostawia ostatnia znana wartosc\r\n");
    }
    else if (cli_equals(cmd, "lcd on"))
    {
        APP_SetLcdPower(1);
        CLI_Printf("LCD: ON\r\n");
    }
    else if (cli_equals(cmd, "lcd off"))
    {
        APP_SetLcdPower(0);
        CLI_Printf("LCD: OFF; pomiary nadal moga dzialac\r\n");
    }
    else if (strncmp(cmd, "refresh", 7) == 0)
    {
        char *arg = cmd + 7;
        while (*arg && isspace((unsigned char)*arg)) arg++;

        if (*arg == '\0')
        {
            CLI_Printf("Blad: uzycie: refresh [1..5]\r\n");
            return;
        }

        int rate = atoi(arg);
        if (APP_SetRefreshRate((uint8_t)rate))
        {
            CLI_Printf("Refresh: RATE=%d, okres=%lu ms\r\n",
                       rate, (unsigned long)APP_GetRefreshPeriodMs());
        }
        else
        {
            CLI_Printf("Blad: RATE musi byc z zakresu 1..5\r\n");
        }
    }
    else if (cli_equals(cmd, "status"))
    {
        cli_status();
    }
    else if (cli_equals(cmd, "stats"))
    {
        cli_stats();
    }
    else if (cli_equals(cmd, "stats reset"))
    {
        APP_ResetMinMax();
        CLI_Printf("Min/max: zresetowano\r\n");
    }
    else if (cli_equals(cmd, "log on"))
    {
        APP_SetLogging(1);
        CLI_Printf("Logowanie CSV: ON\r\n");
    }
    else if (cli_equals(cmd, "log off"))
    {
        APP_SetLogging(0);
        CLI_Printf("Logowanie CSV: OFF\r\n");
        }
    else if (cli_equals(cmd, "clear"))
    {
        CLI_Printf("\x1b[2J\x1b[H");
    }
    else
    {
        CLI_Printf("Nieznana komenda: %s\r\n", cmd);
        CLI_Printf("Wpisz: help\r\n");
    }
}

void CLI_Init(UART_HandleTypeDef *huart)
{
    cli_huart = huart;
    rx_pos = 0;
    cmd_ready = 0;
    cli_esc_state = 0;
    cli_history_nav = -1;
    cli_history_count = 0;
    cli_history_head = 0;
    memset(rx_buf, 0, sizeof(rx_buf));
    memset(cmd_buf, 0, sizeof(cmd_buf));
    cli_start_rx();
}

void CLI_Process(void)
{
    if (!cmd_ready)
    {
        return;
    }

    __disable_irq();
    strncpy(cmd_buf, rx_buf, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_pos = 0;
    cmd_ready = 0;
    __enable_irq();

    {
        char history_entry[CLI_RX_BUF_SIZE];
        strncpy(history_entry, cmd_buf, sizeof(history_entry) - 1);
        history_entry[sizeof(history_entry) - 1] = '\0';
        cli_trim(history_entry);
        cli_history_push(history_entry);
        cli_history_nav = -1;
    }

    cli_execute(cmd_buf);
    CLI_Printf("> ");
}

void CLI_OnUartRxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != cli_huart)
    {
        return;
    }

    char c = (char)rx_byte;

    if (!cmd_ready)
    {
        if (cli_esc_state == 1)
        {
            cli_esc_state = (c == '[') ? 2 : 0;
        }
        else if (cli_esc_state == 2)
        {
            cli_esc_state = 0;
            if (c == 'A')
            {
                cli_history_recall(1);
            }
            else if (c == 'B')
            {
                cli_history_recall(-1);
            }
        }
        else if (c == 0x1B)
        {
            cli_esc_state = 1;
        }
        else if (c == '\r' || c == '\n')
        {
            rx_buf[rx_pos] = '\0';
            cmd_ready = 1;
            CLI_Printf("\r\n");
        }
        else if (c == '\b' || c == 0x7F)
        {
            if (rx_pos > 0)
            {
                rx_pos--;
                rx_buf[rx_pos] = '\0';
                HAL_UART_Transmit(cli_huart, (uint8_t *)"\b \b", 3, 50);
            }
        }
        else if (rx_pos < (CLI_RX_BUF_SIZE - 1))
        {
            rx_buf[rx_pos++] = c;
            HAL_UART_Transmit(cli_huart, (uint8_t *)&c, 1, 50);
        }
    }

    cli_start_rx();
}

void CLI_OnUartErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == cli_huart)
    {
        cli_start_rx();
    }
}
