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

/*
 * Налаштування виводів тими самими полями GPIO_InitTypeDef, що й HAL_GPIO_Init (RM0490 §7.4):
 * MODER, OTYPER, OSPEEDR, PUPDR, AFR і для режимів GPIO_MODE_IT_* — лінія EXTI. Регістровий
 * варіант замість HAL_GPIO_Init звільнив флеш під ядро FreeRTOS (ЛР4, docs/metrics/memory.md).
 */
#define MODE_EXTI      0x10000000u /* біти поля Mode у stm32c0xx_hal_gpio.c: GPIO_MODE_IT_* і EVT_* */
#define MODE_EXTI_IT   0x00010000u
#define MODE_RISING    0x00100000u
#define MODE_FALLING   0x00200000u

void board_gpio_init(GPIO_TypeDef *port, const GPIO_InitTypeDef *g)
{
  uint32_t mode = g->Mode & 3u; /* 0 вхід, 1 вихід, 2 AF, 3 аналог */
  for (uint32_t pin = 0; pin < 16u; pin++) {
    uint32_t bit = 1u << pin;
    if ((g->Pin & bit) == 0u) {
      continue;
    }
    uint32_t sh2 = pin * 2u;
    if (mode == 2u) {
      uint32_t sh4 = (pin & 7u) * 4u;
      port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << sh4)) | (g->Alternate << sh4);
    }
    if (mode == 1u || mode == 2u) {
      port->OSPEEDR = (port->OSPEEDR & ~(3u << sh2)) | (g->Speed << sh2);
      port->OTYPER = (port->OTYPER & ~bit) | (((g->Mode >> 4) & 1u) << pin);
    }
    port->PUPDR = (port->PUPDR & ~(3u << sh2)) | (g->Pull << sh2);
    port->MODER = (port->MODER & ~(3u << sh2)) | (mode << sh2);
    if (g->Mode & MODE_EXTI) {
      uint32_t idx = port == GPIOA ? 0u : port == GPIOB ? 1u : port == GPIOC ? 2u : port == GPIOD ? 3u : 5u;
      uint32_t sh8 = (pin & 3u) * 8u;
      EXTI->EXTICR[pin >> 2] = (EXTI->EXTICR[pin >> 2] & ~(0xFFu << sh8)) | (idx << sh8);
      EXTI->RTSR1 = (g->Mode & MODE_RISING) ? (EXTI->RTSR1 | bit) : (EXTI->RTSR1 & ~bit);
      EXTI->FTSR1 = (g->Mode & MODE_FALLING) ? (EXTI->FTSR1 | bit) : (EXTI->FTSR1 & ~bit);
      EXTI->IMR1 = (g->Mode & MODE_EXTI_IT) ? (EXTI->IMR1 | bit) : (EXTI->IMR1 & ~bit);
    }
  }
}
