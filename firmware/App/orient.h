/**
 * @file    orient.h
 * @brief   Нахил кисті з вектора прискорення (статичний випадок) і
 *          перетворення в кути сервоприводів pan/tilt.
 */
#ifndef ORIENT_H
#define ORIENT_H

#include <stdint.h>

typedef struct {
  int32_t pitch_cdeg; /**< навколо осі Y, соті градуса */
  int32_t roll_cdeg;  /**< навколо осі X, соті градуса */
} orient_t;

/** pitch = atan2(-ax, sqrt(ay²+az²)), roll = atan2(ay, az). */
void orient_from_accel(const int32_t accel_mg[3], orient_t *o);

/** Кут нахилу (−90..+90°) -> кут серво 0..180° (90° = горизонталь). */
int32_t orient_to_servo_deg(int32_t angle_cdeg);

#endif /* ORIENT_H */
