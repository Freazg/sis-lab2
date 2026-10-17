/**
 * @file    board.c
 * @brief   Визначення хендлів периферії (оголошені в board.h).
 */
#include "board.h"

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
I2C_HandleTypeDef  hi2c1;
SPI_HandleTypeDef  hspi1;
TIM_HandleTypeDef  htim3;
