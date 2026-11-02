/**
 * @file    Rtos/wokwi/portmacro.h
 * @brief   Лише для BOARD_TARGET=wokwi. Модель Cortex-M0+ у Wokwi не маскує переривання за PRIMASK:
 *          після `cpsid i` регістр читається як 1, але TIM14 і SysTick далі виконуються (дослід
 *          MASKTEST, docs/rtos-design.md). Порт ARM_CM0 будує на PRIMASK усі критичні секції, тому
 *          в симуляторі списки ядра псувались. Тут лише чотири макроси маскування замінено на
 *          емуляцію PRIMASK (Core/Src/stm32c0xx_it.c): лінії NVIC вимикаються, а SysTick і PendSV,
 *          що прийшли під маскою, виконуються після її зняття. Решта порту — оригінал V11.3.1
 *          (#include_next). Збірка для плати (BOARD_TARGET=nucleo) цей файл не бачить.
 */
#ifndef WOKWI_PORTMACRO_H
#define WOKWI_PORTMACRO_H

#include_next "portmacro.h"

#include <stdint.h>

uint32_t wokwi_mask_from_isr(void);
void wokwi_clear_from_isr(uint32_t prev);
void wokwi_disable_irq(void);
void wokwi_enable_irq(void);

#undef portSET_INTERRUPT_MASK_FROM_ISR
#undef portCLEAR_INTERRUPT_MASK_FROM_ISR
#undef portDISABLE_INTERRUPTS
#undef portENABLE_INTERRUPTS
#define portSET_INTERRUPT_MASK_FROM_ISR()       wokwi_mask_from_isr()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR( x )  wokwi_clear_from_isr( x )
#define portDISABLE_INTERRUPTS()                wokwi_disable_irq()
#define portENABLE_INTERRUPTS()                 wokwi_enable_irq()

#endif /* WOKWI_PORTMACRO_H */
