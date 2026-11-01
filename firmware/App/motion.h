/**
 * @file    motion.h
 * @brief   Обробка руху: комплементарний фільтр (FR-02), мертва зона і обмеження швидкості (FR-05).
 *          Цілочислова арифметика: на Cortex-M0+ немає FPU і апаратного ділення.
 */
#ifndef MOTION_H
#define MOTION_H

#include <stdint.h>

#include "orient.h"

#define MOTION_ALPHA_PERMILLE 980    /* частка гіроскопа; τ ≈ dt·α/(1−α) = 245 мс при 200 Гц */
#define MOTION_DEADZONE_CDEG  150    /* FR-05: ±1.5° */
#define MOTION_MAX_RATE_CDPS  30000  /* FR-05: 300 °/с */
#define MOTION_DT_MAX_US      50000u /* після довгої операції гіроскоп не інтегрується на всю паузу */

typedef struct {
  orient_t est;          /**< оцінка нахилу */
  orient_t zero;         /**< нуль після калібрування `cal` */
  int32_t gyro_bias[3];  /**< мілі-°/с */
  uint8_t started;
} motion_t;

void motion_reset(motion_t *m);

/** Один крок фільтра. gyro X — roll, gyro Y — pitch. dt_us обмежується MOTION_DT_MAX_US. */
void motion_fuse(motion_t *m, const int32_t accel_mg[3], const int32_t gyro_mdps[3], uint32_t dt_us);

/** Новий кут серво: у межах мертвої зони стоїть, інакше йде до цілі не швидше за max_step. */
int32_t motion_slew(int32_t current_cdeg, int32_t target_cdeg, int32_t deadzone_cdeg, int32_t max_step_cdeg);

/** Кут нахилу -> кут серво, соті градуса (0..18000, 90° = горизонталь). */
int32_t motion_to_servo_cdeg(int32_t angle_cdeg);

#endif /* MOTION_H */
