/**
 * @file    board.h
 * @brief   ЄДИНЕ місце знань про плату: піни, alternate functions, хендли периферії,
 *          параметри шин. Відповідає таблиці docs/pinout.md рядок у рядок.
 *
 * Плата: NUCLEO-C031C6 (MB1717), MCU STM32C031C6T6 (LQFP48).
 * Джерела: DS13867 Rev 4 (Table 12 p.31-34, Tables 13/15 AF p.35-37),
 *          UM2953 Rev 2 (Table 10 SB p.17-18, Table 11 Arduino p.20),
 *          RM0490 Rev 6 (Table 115 I2C timing p.711, §6.2.11 timer clock p.128).
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32c0xx_hal.h"

#define BOARD_NAME "NUCLEO-C031C6"
#define FW_VERSION "lr2-1.0"

/* ---- Тактування --------------------------------------------------------- */
#define BOARD_SYSCLK_HZ  48000000u /* HSI48 / HSIDIV=1; FLASH latency 1 WS (RM0490 Table 14 p.59) */
#define BOARD_PCLK_HZ    48000000u /* APB prescaler 1 -> TIMPCLK = PCLK (RM0490 §6.2.11) */

/* ---- USART2 -> ST-LINK VCP (консоль, CLI, self-test) -------------------- */
/* PA2 AF1 USART2_TX, PA3 AF1 USART2_RX (DS13867 Table 13); SB27/SB32 ON (UM2953 Table 10) */
#define CON_TX_PORT  GPIOA
#define CON_TX_PIN   GPIO_PIN_2
#define CON_RX_PORT  GPIOA
#define CON_RX_PIN   GPIO_PIN_3
#define CON_AF       GPIO_AF1_USART2
#define CON_BAUD     115200u        /* BRR = 48e6/115200 = 416.67 -> 417, похибка -0.08 % */

/* ---- USART1 -> FT232RL (резервний UART) --------------------------------- */
/* PB6 AF0 USART1_TX (D1), PB7 AF0 USART1_RX (D0); SB31/SB33 OFF за замовчуванням */
#define AUX_TX_PORT  GPIOB
#define AUX_TX_PIN   GPIO_PIN_6
#define AUX_RX_PORT  GPIOB
#define AUX_RX_PIN   GPIO_PIN_7
#define AUX_AF       GPIO_AF0_USART1
#define AUX_BAUD     115200u

/* ---- I2C1 -> MPU-6050 (0x68) ------------------------------------------- */
/* PB8 AF6 I2C1_SCL (D15), PB9 AF6 I2C1_SDA (D14) — FT_f (DS13867 Table 12, Table 15) */
#define I2C_SCL_PORT GPIOB
#define I2C_SCL_PIN  GPIO_PIN_8
#define I2C_SDA_PORT GPIOB
#define I2C_SDA_PIN  GPIO_PIN_9
#define I2C_AF       GPIO_AF6_I2C1
/* Fast-mode 400 кГц, I2CCLK = PCLK = 48 МГц (RCC_CCIPR.I2C1SEL = 00):
 * RM0490 Table 115: tPRESC=125 нс (PRESC=5), SCLL=10, SCLH=4, SDADEL=3, SCLDEL=4 (поля = N-1 для SCL*) */
#define I2C_TIMINGR_400K 0x50330309u
#define I2C_TIMEOUT_MS   5u
/* Repeated START (HAL_I2C_Mem_Read). Модель I2C STM32C0 у Wokwi його не підтримує
 * (docs/captures/analysis.md, аномалія A2), тому для симулятора — STOP між записом
 * номера регістра і читанням. Для реальної плати: make build BOARD_TARGET=nucleo */
#ifndef BOARD_I2C_REPEATED_START
#define BOARD_I2C_REPEATED_START 0
#endif
/* HAL_I2C_Master_Transmit для запису. У Wokwi замінено регістровою реалізацією (аномалія A3). */
#ifndef BOARD_I2C_HAL_TRANSMIT
#define BOARD_I2C_HAL_TRANSMIT 0
#endif

/* ---- SPI1 -> W25Q64 ---------------------------------------------------- */
/* PB3 AF0 SPI1_SCK (D3), PA6 AF0 SPI1_MISO (D12), PA7 AF0 SPI1_MOSI (D11) — Tables 13/15;
 * CS# — PB0 (D10) як звичайний GPIO (програмний CS) */
#define SPI_SCK_PORT  GPIOB
#define SPI_SCK_PIN   GPIO_PIN_3
#define SPI_MISO_PORT GPIOA
#define SPI_MISO_PIN  GPIO_PIN_6
#define SPI_MOSI_PORT GPIOA
#define SPI_MOSI_PIN  GPIO_PIN_7
#define SPI_AF        GPIO_AF0_SPI1
#define FLASH_CS_PORT GPIOB
#define FLASH_CS_PIN  GPIO_PIN_0
#define SPI_PRESCALER SPI_BAUDRATEPRESCALER_16 /* 48 МГц / 16 = 3 МГц, mode 0 */
#define SPI_TIMEOUT_MS 5u

/* ---- TIM3 PWM -> SG92R ×2 ---------------------------------------------- */
/* PB4 AF1 TIM3_CH1 (D5), PB5 AF1 TIM3_CH2 (D6) — Table 15 */
#define SERVO_PAN_PORT  GPIOB
#define SERVO_PAN_PIN   GPIO_PIN_4
#define SERVO_TILT_PORT GPIOB
#define SERVO_TILT_PIN  GPIO_PIN_5
#define SERVO_AF        GPIO_AF1_TIM3
#define SERVO_PAN_CH    TIM_CHANNEL_1
#define SERVO_TILT_CH   TIM_CHANNEL_2
#define SERVO_TIM_PSC   47u    /* 48 МГц / (47+1) = 1 МГц -> 1 тік = 1 мкс */
#define SERVO_TIM_ARR   19999u /* (19999+1) мкс = 20 мс -> 50 Гц */

/* ---- GPIO --------------------------------------------------------------- */
#define LED_LIVE_PORT GPIOA /* D7, PA15 — зелений LED «Live» */
#define LED_LIVE_PIN  GPIO_PIN_15
#define LED_REC_PORT  GPIOB /* D4, PB10 — червоний LED «Recording/Hold» (pin без AF) */
#define LED_REC_PIN   GPIO_PIN_10
#define BTN_PORT      GPIOC /* B1 USER (UM2953 §6.11); у Wokwi — кнопка на GND */
#define BTN_PIN       GPIO_PIN_13
#define BTN_ACTIVE_LOW 1
#define MARK_PORT     GPIOA /* D8, PA9 — службовий маркер подій для логічного аналізатора */
#define MARK_PIN      GPIO_PIN_9

/* ---- Хендли периферії (визначені в board.c) ----------------------------- */
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef  hi2c1;
extern SPI_HandleTypeDef  hspi1;
extern TIM_HandleTypeDef  htim3;

#endif /* BOARD_H */
