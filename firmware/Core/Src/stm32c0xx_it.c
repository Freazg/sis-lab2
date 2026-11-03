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

#if BOARD_WOKWI
/*
 * Емуляція PRIMASK для симулятора (Rtos/wokwi/portmacro.h). Модель Cortex-M0+ у Wokwi ставить біт
 * PRIMASK, але переривання ним не блокуються. Під маскою вимикаються лінії NVIC усіх джерел, що
 * викликають ядро або HAL (TIM14, EXTI4_15, USART2); їхні прапорці лишаються, і обробники
 * виконуються одразу після зняття маски. SysTick і PendSV вимкнути в NVIC не можна, тому їхні
 * обгортки нижче відкладають роботу до зняття маски. Справжній cpsid теж виконується.
 */
#define WOKWI_IRQS ((1UL << TIM14_IRQn) | (1UL << EXTI4_15_IRQn) | (1UL << USART2_IRQn))
static volatile uint32_t wokwi_primask;
static volatile uint32_t wokwi_ticks_pending;
static volatile uint32_t wokwi_pendsv_deferred;

void wokwi_disable_irq(void)
{
  __disable_irq();
  NVIC->ICER[0] = WOKWI_IRQS;
  wokwi_primask = 1;
}

void wokwi_enable_irq(void)
{
  /* Тік, що прийшов під маскою, на кристалі виконався б одразу після cpsie. Тому в режимі задачі
   * відкладені тіки обробляються тут, ще під маскою, а перемикання (PENDSVSET) — одразу після її зняття.
   * Без цього imu прокидалась на тік пізніше: джитер ±1 мс замість мікросекунд (журнал проблем ЛР4).
   * У режимі обробника відкладені тіки обробляє цикл у SysTick_Handler. */
  if (__get_IPSR() == 0u && wokwi_ticks_pending != 0u)
  {
    wokwi_disable_irq(); /* обробник тіку виконується під маскою, як у SysTick_Handler */
  }
  while (__get_IPSR() == 0u && wokwi_ticks_pending != 0u)
  {
    wokwi_ticks_pending--;
    uint32_t t0 = platform_cycles();
    xPortSysTickHandler();
    rtos_tick_cost(platform_cycles() - t0);
  }
  wokwi_primask = 0;
  NVIC->ISER[0] = WOKWI_IRQS;
  if (wokwi_pendsv_deferred)
  {
    wokwi_pendsv_deferred = 0;
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
  }
  __enable_irq();
}

uint32_t wokwi_mask_from_isr(void)
{
  uint32_t prev = wokwi_primask;
  wokwi_disable_irq();
  return prev;
}

void wokwi_clear_from_isr(uint32_t prev)
{
  if (prev == 0u)
  {
    wokwi_enable_irq();
  }
}

/** PendSV під маскою відкладається: перемикання контексту всередині критичної секції псує списки. */
void xPortPendSVHandler(void);
__attribute__((naked)) void PendSV_Handler(void)
{
  __asm volatile(" ldr r0, =wokwi_primask        \n"
                 " ldr r0, [r0]                  \n"
                 " cmp r0, #0                    \n"
                 " beq 1f                        \n"
                 " ldr r0, =wokwi_pendsv_deferred \n"
                 " movs r1, #1                   \n"
                 " str r1, [r0]                  \n"
                 " bx lr                         \n"
                 "1:                             \n"
                 " ldr r0, =xPortPendSVHandler   \n"
                 " bx r0                         \n");
}
#endif

/** SysTick — тік FreeRTOS (configTICK_RATE_HZ), найнижчий пріоритет. */
void SysTick_Handler(void)
{
#if BOARD_WOKWI
  if (wokwi_primask)
  {
    wokwi_ticks_pending++; /* тік під маскою: обробиться з наступним, лічба тіків не губиться */
    return;
  }
  do
  {
#endif
    uint32_t t0 = platform_cycles();
    xPortSysTickHandler();
    rtos_tick_cost(platform_cycles() - t0);
#if BOARD_WOKWI
  } while (wokwi_ticks_pending != 0u && wokwi_ticks_pending-- != 0u);
#endif
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
