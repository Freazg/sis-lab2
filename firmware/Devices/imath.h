/**
 * @file    imath.h
 * @brief   Цілочислова математика без float (Cortex-M0+ не має FPU).
 */
#ifndef IMATH_H
#define IMATH_H

#include <stdint.h>

/** Ділення з округленням до найближчого (half away from zero). den > 0. */
static inline int32_t idiv_round(int32_t num, int32_t den)
{
  return (num >= 0) ? (num + den / 2) / den : -((-num + den / 2) / den);
}

/** floor(sqrt(v)), побітовий метод — без ділень. */
uint32_t isqrt_u64(uint64_t v);

/**
 * atan2(y, x) у сотих частках градуса, діапазон (-18000, 18000].
 * Апроксимація atan(z) ≈ π/4·z + 0.273·z·(1−|z|) для |z| ≤ 1, похибка < 0.25°.
 */
int32_t iatan2_cdeg(int32_t y, int32_t x);

#endif /* IMATH_H */
