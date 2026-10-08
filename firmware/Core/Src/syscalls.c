/**
  ******************************************************************************
  * @file    syscalls.c
  * @brief   newlib-хуки: printf() -> USART2 (ST-Link VCP / Wokwi Serial Monitor).
  *          Решту системних викликів (_sbrk, _close, ...) дає -specs=nosys.specs.
  ******************************************************************************
  */
#include "main.h"

int _write(int fd, char *p, int n)
{
  (void)fd;
  HAL_UART_Transmit(&huart2, (uint8_t *)p, (uint16_t)n, HAL_MAX_DELAY);
  return n;
}
