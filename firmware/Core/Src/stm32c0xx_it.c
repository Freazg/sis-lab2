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
 * PRIMASK, але переривання ним не блокуються. На кристалі переривання, що прийшло в критичній секції,
 * чекає мікросекунди; у Wokwi та сама секція в ≈ 7 разів довша, і вимкнена лінія USART2 губила байти
 * команд (журнал проблем ЛР4). Тому лінії NVIC не вимикаються: обробники EXTI і USART2 виконуються
 * одразу й фіксують мітку часу та байт, а під маскою відкладається лише їхній виклик ядра
 * (xTaskNotifyFromISR). SysTick і PendSV під маскою теж відкладаються. Відкладене виконується в момент
 * зняття маски, як виконалось би на кристалі після cpsie (подробиці — у wokwi_enable_irq).
 */
#define WOKWI_DEFER_BTN 0x1u
#define WOKWI_DEFER_RX  0x2u
static volatile uint32_t wokwi_primask;
static volatile uint32_t wokwi_ticks_pending;
static volatile uint32_t wokwi_pendsv_deferred;
static volatile uint32_t wokwi_isr_deferred;

void wokwi_disable_irq(void)
{
  __disable_irq();
  wokwi_primask = 1;
}

/** Відкладені виклики ядра з ISR; викликається під маскою, тож нові ISR лише додають біти. */
static void wokwi_flush_isr(void)
{
  uint32_t bits;
  while ((bits = wokwi_isr_deferred) != 0u)
  {
    wokwi_isr_deferred = 0;
    if (bits & WOKWI_DEFER_BTN)
    {
      rtos_btn_from_isr();
    }
    if (bits & WOKWI_DEFER_RX)
    {
      rtos_con_rx_from_isr();
    }
  }
}

void wokwi_enable_irq(void)
{
  /* Що прийшло під маскою, на кристалі виконалось би одразу після cpsie, тож виконується тут, ще під
   * маскою; перемикання (PENDSVSET) — одразу після її зняття. Виклики ядра з EXTI і USART2 — за будь-якого
   * зняття маски: ці переривання вищі за PendSV і на кристалі витіснили б його одразу після cpsie. Тік —
   * лише в режимі задачі: SysTick має пріоритет PendSV і виконався б після нього (у режимі обробника
   * відкладені тіки обробляє SysTick_Handler). Без повтору тіку imu прокидалась на тік пізніше: джитер
   * ±1 мс (журнал проблем ЛР4). */
  bool thread = __get_IPSR() == 0u;
  while (wokwi_isr_deferred != 0u || (thread && wokwi_ticks_pending != 0u))
  {
    wokwi_disable_irq();
    while (thread && wokwi_ticks_pending != 0u)
    {
      wokwi_ticks_pending--;
      uint32_t t0 = platform_cycles();
      xPortSysTickHandler();
      rtos_tick_cost(platform_cycles() - t0);
    }
    wokwi_flush_isr();
    wokwi_primask = 0; /* ISR між цим рядком і перевіркою циклу вже не відкладаються */
  }
  wokwi_primask = 0;
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

/** true — маска стоїть, виклик ядра відкладено до її зняття. */
static bool wokwi_defer(uint32_t bit)
{
  if (wokwi_primask)
  {
    wokwi_isr_deferred |= bit;
    return true;
  }
  return false;
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
#if BOARD_WOKWI
  if (wokwi_defer(WOKWI_DEFER_BTN))
  {
    return;
  }
#endif
  rtos_btn_from_isr(); /* розбудити задачу btn */
  /* USER CODE END EXTI4_15_IRQn 0 */
}

/** USART2 (ST-LINK VCP): прийом команд консолі. */
void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */
  platform_con_rx_isr();
#if BOARD_WOKWI
  if (wokwi_defer(WOKWI_DEFER_RX))
  {
    return;
  }
#endif
  rtos_con_rx_from_isr(); /* розбудити задачу cli */
  /* USER CODE END USART2_IRQn 0 */
}
