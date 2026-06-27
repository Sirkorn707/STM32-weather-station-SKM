#ifndef CLI_H
#define CLI_H

#include "main.h"
#include <stdint.h>

void CLI_Init(UART_HandleTypeDef *huart);
void CLI_Process(void);

void CLI_OnUartRxCpltCallback(UART_HandleTypeDef *huart);
void CLI_OnUartErrorCallback(UART_HandleTypeDef *huart);

void CLI_Printf(const char *fmt, ...);

#endif
