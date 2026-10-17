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
dev_status_t platform_aux_write(const char *s, size_t len);

dev_status_t platform_i2c_mem_read_restart(uint8_t addr7, uint8_t reg, uint8_t *val);
void platform_i2c_recover(void);

uint32_t platform_sysclk_hz(void);

#endif /* PLATFORM_H */
