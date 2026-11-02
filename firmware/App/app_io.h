/**
 * @file    app_io.h
 * @brief   Форматований вивід у консоль через app_hw_t (без stdio і heap).
 */
#ifndef APP_IO_H
#define APP_IO_H

#include <stdarg.h>

#include "app_hw.h"

#if defined(__GNUC__)
#define APP_PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))
#else
#define APP_PRINTF_FMT(a, b)
#endif

/** Підмножина snprintf: %c %s %d %i %u %x %X, модифікатор l, ширина. Повертає потрібну довжину. */
int app_vformat(char *buf, size_t size, const char *fmt, va_list ap);
int app_format(char *buf, size_t size, const char *fmt, ...) APP_PRINTF_FMT(3, 4);

/** printf у консоль; рядок обрізається до 160 символів. */
void app_print(const app_hw_t *hw, const char *fmt, ...) APP_PRINTF_FMT(2, 3);

/** Відкладений друк (ЛР4): рядок до 96 символів іде в чергу задачі log, задача-відправник не
 *  чекає UART. Для задач із жорстким періодом (imu). */
void app_printq(const app_hw_t *hw, const char *fmt, ...) APP_PRINTF_FMT(2, 3);

/** Те саме, але в резервний канал (FT232RL). Повертає статус передачі. */
dev_status_t app_print_aux(const app_hw_t *hw, const char *fmt, ...) APP_PRINTF_FMT(2, 3);

#endif /* APP_IO_H */
