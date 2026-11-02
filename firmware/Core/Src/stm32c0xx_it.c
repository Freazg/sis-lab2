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
#include "rtos_app.h"
/* USER CODE END Includes */

/* Обробник тіку FreeRTOS: порт ARM_CM0 зібрано з -DSysTick_Handler=xPortSysTickHandler
 * (firmware/Makefile), щоб тут виміряти тривалість тіку ОС (методичка ЛР4 §6.5).
 * SVC_Handler і PendSV_Handler визначає сам порт (portasm.c). */
void xPortSysTickHandler(void);

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

/** SysTick — тік FreeRTOS (configTICK_RATE_HZ), найнижчий пріоритет. */
void SysTick_Handler(void)
{
  uint32_t t0 = platform_cycles();
  xPortSysTickHandler();
  rtos_tick_cost(platform_cycles() - t0);
}

/** TIM14 1 кГц — база часу HAL (HAL_GetTick / HAL_Delay) і лічильник тактів, пріоритет 0. */
void TIM14_IRQHandler(void)
{
  platform_tick_isr();
}

/** EXTI13 (B1): мітка часу події; вся реакція — у задачі btn. */
void EXTI4_15_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI4_15_IRQn 0 */
  platform_btn_isr();
  rtos_btn_from_isr(); /* розбудити задачу btn */
  /* USER CODE END EXTI4_15_IRQn 0 */
}

/** USART2 (ST-LINK VCP): прийом команд консолі. */
void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */
  platform_con_rx_isr();
  rtos_con_rx_from_isr(); /* розбудити задачу cli */
  /* USER CODE END USART2_IRQn 0 */
}
