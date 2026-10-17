/**
  ******************************************************************************
  * @file    stm32c0xx_it.c
  * @brief   Обробники переривань Cortex-M0+.
  ******************************************************************************
  */
#include "main.h"
#include "stm32c0xx_it.h"
/* USER CODE BEGIN Includes */
#include "platform.h"
/* USER CODE END Includes */

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/** SysTick 1 kHz — база часу HAL (HAL_GetTick / HAL_Delay). */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/** USART2 (ST-LINK VCP): прийом команд консолі. */
void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */
  platform_con_rx_isr();
  /* USER CODE END USART2_IRQn 0 */
}
