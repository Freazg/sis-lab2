/**
 * @file    selftest.h
 * @brief   Режим самоперевірки (bring-up) — зведення стану кожного пристрою PRD.
 *
 * Формат виводу (по одному рядку на пристрій, придатний для автоматичного розбору):
 *
 *   ST,dev=<NAME>,res=<PASS|FAIL>[,key=value]...
 *
 * Перший рядок — ST,dev=BEGIN,..., останній — ST,dev=SUMMARY,res=...,pass=N,fail=M.
 * Ключі й значення не містять ком і пробілів. Розбір: tools/parse_selftest.py.
 */
#ifndef SELFTEST_H
#define SELFTEST_H

#include <stdint.h>

#include "app_hw.h"

/* Пороги правдоподібності (обґрунтування — у docs/pinout.md §5 і docs/lab2-report.md). */
#define ST_ACCEL_NORM_MG       1000
#define ST_ACCEL_TOL_MG        110   /* ±3 % чутливості + ±80 mg zero-g по Z, [PS] §6.2 p.13 */
#define ST_GYRO_MAX_MDPS       20000 /* initial ZRO tolerance ±20 °/с, [PS] §6.1 p.12 */
#define ST_TEMP_MIN_CDEG       500   /* «кімнатна» температура стенда: 5..45 °C */
#define ST_TEMP_MAX_CDEG       4500
#define ST_FLASH_TEST_ADDR     0x7FF000u /* останній сектор 4 KB: не перетинається з даними журналу */
#define ST_FLASH_TEST_LEN      32u
#define ST_PULSE_TOL_US        2u        /* 1 тік TIM3 = 1 мкс; округлення */

typedef struct {
  uint8_t pass;
  uint8_t fail;
} selftest_result_t;

/** Запустити всі перевірки й надрукувати зведення. */
selftest_result_t selftest_run(const app_hw_t *hw);

/** Шаблон даних для перевірки флеш: байт i = (i * 37 + seed) & 0xFF. */
void selftest_flash_pattern(uint8_t *buf, uint32_t len, uint8_t seed);

#endif /* SELFTEST_H */
