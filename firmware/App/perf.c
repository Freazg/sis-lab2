#include "perf.h"

#include <string.h>

void perf_add(perf_stat_t *s, uint32_t value)
{
  if (s->count == 0 || value < s->min) {
    s->min = value;
  }
  if (value > s->max) {
    s->max = value;
  }
  s->sum += value;
  s->count++;
}

uint32_t perf_avg(const perf_stat_t *s)
{
  return s->count ? (uint32_t)(s->sum / s->count) : 0u;
}

void perf_jitter_reset(perf_jitter_t *j, uint32_t period_us, uint32_t tol_us)
{
  memset(j, 0, sizeof *j);
  j->period_us = period_us;
  j->tol_us = tol_us;
}

void perf_jitter_add(perf_jitter_t *j, uint32_t dt_us)
{
  int32_t jit = (int32_t)dt_us - (int32_t)j->period_us;
  uint32_t mag = jit < 0 ? (uint32_t)(-jit) : (uint32_t)jit;
  if (j->n == 0 || jit < j->min_us) {
    j->min_us = jit;
  }
  if (j->n == 0 || jit > j->max_us) {
    j->max_us = jit;
  }
  j->n++;
  if (dt_us > 2u * j->period_us) {
    j->missed++;
  }
  if (mag > j->tol_us) {
    j->viol++;
  }
  uint32_t bin = mag / PERF_HIST_BIN_US;
  j->hist[bin < PERF_HIST_BINS ? bin : PERF_HIST_BINS - 1u]++;
}

uint32_t perf_jitter_percentile_us(const perf_jitter_t *j, uint32_t pct)
{
  uint64_t need = ((uint64_t)j->n * pct + 99u) / 100u;
  uint64_t acc = 0;
  for (uint32_t i = 0; i < PERF_HIST_BINS; i++) {
    acc += j->hist[i];
    if (acc >= need) {
      return (i + 1u) * PERF_HIST_BIN_US;
    }
  }
  return PERF_HIST_BINS * PERF_HIST_BIN_US;
}
