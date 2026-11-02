/**
 * @file    rtos_app.h
 * @brief   Організація виконання на FreeRTOS (ЛР4): задачі, черги, м'ютекси, хуки, метрики ОС.
 *          Rtos/ — єдиний шар, що знає про ядро; App/ бачить ОС лише через app_hw_t.
 *          Схема задач і обґрунтування пріоритетів — docs/rtos-design.md.
 */
#ifndef RTOS_APP_H
#define RTOS_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app.h"

/* Реалізації полів app_hw_t (заповнює Core/main.c) */
bool rtos_lock(app_lock_t id, uint32_t timeout_ms);
void rtos_unlock(app_lock_t id);
void rtos_crit(bool enter);
void rtos_log_post(const char *s, size_t len);
void rtos_mode_post(int mode);
void rtos_rec_post(int16_t pan, int16_t tilt);
void rtos_long_post(app_long_op_t op);
void rtos_os_cmd(int id);
bool rtos_delay_ms(uint32_t ms);  /**< vTaskDelay після старту планувальника; false — ще не запущено */
uint32_t rtos_tick_us(void);

/** Створити об'єкти ядра і задачі, запустити планувальник. Не повертається. */
void rtos_start(app_t *app, const app_hw_t *hw);

/* Виклики з обробників переривань (Core/Src/stm32c0xx_it.c) */
void rtos_btn_from_isr(void);
void rtos_con_rx_from_isr(void);
void rtos_tick_cost(uint32_t cycles); /**< тривалість обробника SysTick, такти (§6.5) */

#endif /* RTOS_APP_H */
