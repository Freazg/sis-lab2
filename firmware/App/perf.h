/**
 * @file    perf.h
 * @brief   Вимірювання часу за методичкою ЛР3 §5.1: статистика min/avg/max, джитер, гістограма.
 *          Чиста логіка над значеннями тактів; джерело часу — app_hw_t.cycles.
 */
#ifndef PERF_H
#define PERF_H

#include <stdint.h>

typedef struct {
  uint32_t count, min, max;
  uint64_t sum;
} perf_stat_t;

#define PERF_HIST_BINS   16u
#define PERF_HIST_BIN_US 100u

/** Джитер періодичної задачі: відхилення інтервалу між запусками від заданого періоду. */
typedef struct {
  uint32_t period_us;
  uint32_t tol_us;   /**< допуск вимоги PRD (FR-01: ±500 мкс) */
  int32_t min_us;
  int32_t max_us;
  uint32_t n;
  uint32_t missed;   /**< інтервал > 2 періодів */
  uint32_t viol;     /**< |джитер| > tol_us */
  uint32_t hist[PERF_HIST_BINS]; /**< |джитер| по 100 мкс; останній кошик — усе, що більше */
} perf_jitter_t;

void perf_add(perf_stat_t *s, uint32_t value);
uint32_t perf_avg(const perf_stat_t *s);

void perf_jitter_reset(perf_jitter_t *j, uint32_t period_us, uint32_t tol_us);
void perf_jitter_add(perf_jitter_t *j, uint32_t dt_us);
/** Верхня межа кошика, у який потрапляє перцентиль pct (1..100). */
uint32_t perf_jitter_percentile_us(const perf_jitter_t *j, uint32_t pct);

#endif /* PERF_H */
