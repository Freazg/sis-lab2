/**
  ******************************************************************************
  * @file    lcd1602.h
  * @brief   Мінімальний драйвер LCD1602 (HD44780) через I2C-розширювач PCF8574.
  *          Розводка PCF8574 -> HD44780: P0=RS, P1=RW, P2=E, P3=BL, P4..P7=D4..D7.
  ******************************************************************************
  */
#ifndef __LCD1602_H
#define __LCD1602_H

#include "main.h"

#define LCD1602_I2C_ADDR 0x27U /* 7-бітна адреса модуля (A0..A2 = 1) */

HAL_StatusTypeDef lcd1602_init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef lcd1602_set_cursor(uint8_t col, uint8_t row);
HAL_StatusTypeDef lcd1602_print(const char *s);

#endif /* __LCD1602_H */
