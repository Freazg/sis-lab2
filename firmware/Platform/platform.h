/**
 * @file    platform.h
 * @brief   Адаптація до плати: реалізації інтерфейсів Devices/bus.h поверх HAL
 *          і функції GPIO/UART для прикладного шару.
 *          Platform/ може включати HAL і Devices/bus.h, але не App/.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bus.h"

extern const i2c_bus_t   platform_i2c1;
extern const spi_bus_t   platform_spi1_flash;
extern const pwm_out_t   platform_pwm_pan;
extern const pwm_out_t   platform_pwm_tilt;
extern const dev_time_t  platform_time;

/** Індекс LED збігається з app_led_t (0 = LIVE, 1 = REC). */
void platform_led_set(int idx, bool on);
bool platform_led_get(int idx);
bool platform_button_pressed(void);
void platform_mark(bool level);

void platform_con_write(const char *s, size_t len);
int  platform_con_getc(void);
void platform_con_start_rx(void); /**< увімкнути переривання прийому USART2 */
void platform_con_rx_isr(void);   /**< викликається з USART2_IRQHandler */
dev_status_t platform_aux_write(const char *s, size_t len);

dev_status_t platform_i2c_mem_read_restart(uint8_t addr7, uint8_t reg, uint8_t *val);
void platform_i2c_recover(void);

uint32_t platform_sysclk_hz(void);

/* ---- Вимірювання (ЛР3) --------------------------------------------------- */
/**
 * Лічильник тактів ядра 32 біт. На Cortex-M0+ немає DWT CYCCNT (ARMv6-M), тому значення
 * складається з uwTick і поточного SysTick->VAL. Семантика та сама, що в CYCCNT: беззнакова
 * різниця коректна для інтервалів до 2^32 тактів (89,5 с при 48 МГц).
 */
uint32_t platform_cycles(void);
uint32_t platform_cal_loop(void);  /**< такти на 500 ітерацій еталонного циклу */
uint32_t platform_micros(void);    /**< час від скидання, мкс (32 біти, ≈ 71 хв до переповнення) */
void platform_probe(unsigned n, bool level);

/** Викликається з EXTI4_15_IRQHandler: мітка часу першого спаду B1 до обробки. */
void platform_btn_isr(void);
/** Забрати подію кнопки. Повертає true і мітку часу, якщо вона була. */
bool platform_btn_take(uint32_t *t_cycles);

/** Забрати мітку часу прийому кінця рядка ('\n') з черги ISR. */
bool platform_rx_eol_take(uint32_t *t_cycles);
uint32_t platform_rx_dropped(void); /**< байтів втрачено: ринг + апаратний ORE */

void platform_stack_paint(void);    /**< викликати першим рядком main() */
uint32_t platform_stack_used(void);
uint32_t platform_stack_size(void);
void platform_reset(void);

/** Прапорець «сенсор не готовий після старту»: переживає програмне скидання (.noinit). */
void platform_boot_fault_set(uint32_t ms);
uint32_t platform_boot_fault_ms(void);

/** Впорскування відмови I2C: 0 — немає, 1 — NACK на шині. */
void platform_i2c_fault(unsigned mode);

#endif /* PLATFORM_H */
