/**
  ******************************************************************************
  * @file    main.h
  * @brief   Спільні визначення пінів і хендлів периферії (структура як у CubeMX).
  *          Плата: NUCLEO-C031C6 (у Wokwi), MCU STM32C031C6T6, Cortex-M0+ @ 48 MHz.
  ******************************************************************************
  */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32c0xx_hal.h"

/* ---- Pin map (див. docs/hardware-components.md) ------------------------- */
/* USART2 -> ST-Link VCP / Wokwi UART Logger (Serial Monitor), AF1 */
#define VCP_TX_Pin            GPIO_PIN_2
#define VCP_TX_GPIO_Port      GPIOA
#define VCP_RX_Pin            GPIO_PIN_3
#define VCP_RX_GPIO_Port      GPIOA

/* I2C1 -> MPU-6050 (0x68) + LCD1602 I2C (0x27), AF6, Arduino D15/D14 */
#define I2C1_SCL_Pin          GPIO_PIN_8
#define I2C1_SCL_GPIO_Port    GPIOB
#define I2C1_SDA_Pin          GPIO_PIN_9
#define I2C1_SDA_GPIO_Port    GPIOB

/* ADC1_IN0 -> потенціометр (A0) */
#define POT_Pin               GPIO_PIN_0
#define POT_GPIO_Port         GPIOA
#define POT_ADC_CHANNEL       ADC_CHANNEL_0

/* GPIO */
#define LED_Pin               GPIO_PIN_5   /* LD4, active high */
#define LED_GPIO_Port         GPIOA
#define BTN_Pin               GPIO_PIN_13  /* B1 USER, active low */
#define BTN_GPIO_Port         GPIOC

/* ---- Handles ------------------------------------------------------------ */
extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef  hi2c1;
extern ADC_HandleTypeDef  hadc1;

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
