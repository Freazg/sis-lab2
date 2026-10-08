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
/* USART2 -> ST-Link VCP / Wokwi Serial Monitor, AF1 */
#define VCP_TX_Pin            GPIO_PIN_2
#define VCP_TX_GPIO_Port      GPIOA
#define VCP_RX_Pin            GPIO_PIN_3
#define VCP_RX_GPIO_Port      GPIOA

/* I2C1 -> MPU-6050 (0x68), AF6, Arduino D15/D14 */
#define I2C1_SCL_Pin          GPIO_PIN_8
#define I2C1_SCL_GPIO_Port    GPIOB
#define I2C1_SDA_Pin          GPIO_PIN_9
#define I2C1_SDA_GPIO_Port    GPIOB

/* ADC1_IN0 -> потенціометр (A0) */
#define POT_Pin               GPIO_PIN_0
#define POT_GPIO_Port         GPIOA
#define POT_ADC_CHANNEL       ADC_CHANNEL_0

/* TIM3 PWM -> сервоприводи pan/tilt, AF1 */
#define SERVO_PAN_Pin         GPIO_PIN_4
#define SERVO_PAN_GPIO_Port   GPIOB
#define SERVO_TILT_Pin        GPIO_PIN_5
#define SERVO_TILT_GPIO_Port  GPIOB
#define SERVO_PAN_CHANNEL     TIM_CHANNEL_1
#define SERVO_TILT_CHANNEL    TIM_CHANNEL_2

/* SPI1 -> W25Q64 (на реальному залізі; у Wokwi SPI-флеш відсутній), AF0 */
#define FLASH_SCK_Pin         GPIO_PIN_3
#define FLASH_SCK_GPIO_Port   GPIOB
#define FLASH_MISO_Pin        GPIO_PIN_6
#define FLASH_MISO_GPIO_Port  GPIOA
#define FLASH_MOSI_Pin        GPIO_PIN_7
#define FLASH_MOSI_GPIO_Port  GPIOA
#define FLASH_CS_Pin          GPIO_PIN_0
#define FLASH_CS_GPIO_Port    GPIOB

/* GPIO */
#define LED_Pin               GPIO_PIN_5   /* LD4, зелений, active high */
#define LED_GPIO_Port         GPIOA
#define BTN_Pin               GPIO_PIN_13  /* B1 USER, active low */
#define BTN_GPIO_Port         GPIOC

/* ---- Handles ------------------------------------------------------------ */
extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef  hi2c1;
extern ADC_HandleTypeDef  hadc1;
extern TIM_HandleTypeDef  htim3;
extern SPI_HandleTypeDef  hspi1;

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
