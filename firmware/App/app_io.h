/**
 * @file    app_io.h
 * @brief   Форматований вивід у консоль через app_hw_t (без stdio-хуків HAL).
 */
#ifndef APP_IO_H
#define APP_IO_H

#include "app_hw.h"

#if defined(__GNUC__)
#define APP_PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))
#else
#define APP_PRINTF_FMT(a, b)
#endif

/** printf у консоль; рядок обрізається до 160 символів. */
void app_print(const app_hw_t *hw, const char *fmt, ...) APP_PRINTF_FMT(2, 3);

/** Те саме, але в резервний канал (FT232RL). Повертає статус передачі. */
dev_status_t app_print_aux(const app_hw_t *hw, const char *fmt, ...) APP_PRINTF_FMT(2, 3);

#endif /* APP_IO_H */
