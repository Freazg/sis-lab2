/**
 * @file    servo.h
 * @brief   Аналоговий сервопривід (Tower Pro SG92R) через канал ШІМ 50 Гц.
 *
 * Виробник SG92R публікує лише напругу 4.8 В, швидкість 0.1 с/60° і dead band 1 мкс,
 * без таблиці «ширина імпульсу -> кут». Тому межі імпульсу — параметри калібрування
 * (за замовчуванням 544..2400 мкс, як у бібліотеці Arduino Servo для мікросерво класу 9 г).
 */
#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

#include "bus.h"

#define SERVO_DEFAULT_MIN_US 544u
#define SERVO_DEFAULT_MAX_US 2400u
#define SERVO_PERIOD_US      20000u /* 50 Гц */

typedef struct {
  const pwm_out_t *pwm;
  uint32_t min_us;     /**< імпульс для 0° */
  uint32_t max_us;     /**< імпульс для 180° */
  uint32_t lim_lo_deg; /**< програмні межі ходу */
  uint32_t lim_hi_deg;
  uint32_t angle_deg;  /**< останній заданий (після обмеження) кут */
} servo_t;

void servo_init(servo_t *s, const pwm_out_t *pwm, uint32_t min_us, uint32_t max_us);
void servo_set_limits(servo_t *s, uint32_t lo_deg, uint32_t hi_deg);

/** Чиста функція: кут (0..180) -> ширина імпульсу, з округленням. */
uint32_t servo_pulse_for_deg(uint32_t deg, uint32_t min_us, uint32_t max_us);

/** Обмежити кут межами і записати імпульс у ШІМ. Повертає записаний імпульс, мкс. */
uint32_t servo_set_deg(servo_t *s, int32_t deg);

#endif /* SERVO_H */
