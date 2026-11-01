#include "servo.h"

void servo_init(servo_t *s, const pwm_out_t *pwm, uint32_t min_us, uint32_t max_us)
{
  s->pwm = pwm;
  s->min_us = min_us;
  s->max_us = max_us;
  s->lim_lo_deg = 0;
  s->lim_hi_deg = 180;
  s->angle_deg = 90;
  s->angle_cdeg = 9000;
}

void servo_set_limits(servo_t *s, uint32_t lo_deg, uint32_t hi_deg)
{
  if (hi_deg > 180u) {
    hi_deg = 180u;
  }
  if (lo_deg > hi_deg) {
    lo_deg = hi_deg;
  }
  s->lim_lo_deg = lo_deg;
  s->lim_hi_deg = hi_deg;
}

uint32_t servo_pulse_for_deg(uint32_t deg, uint32_t min_us, uint32_t max_us)
{
  if (deg > 180u) {
    deg = 180u;
  }
  return min_us + (deg * (max_us - min_us) + 90u) / 180u;
}

uint32_t servo_set_cdeg(servo_t *s, int32_t cdeg)
{
  int32_t lo = (int32_t)s->lim_lo_deg * 100;
  int32_t hi = (int32_t)s->lim_hi_deg * 100;
  if (cdeg < lo) {
    cdeg = lo;
  }
  if (cdeg > hi) {
    cdeg = hi;
  }
  s->angle_cdeg = cdeg;
  s->angle_deg = (uint32_t)((cdeg + 50) / 100);
  uint32_t us = s->min_us + ((uint32_t)cdeg * (s->max_us - s->min_us) + 9000u) / 18000u;
  if (s->pwm != 0) {
    s->pwm->set_pulse_us(s->pwm->ctx, us);
  }
  return us;
}

uint32_t servo_set_deg(servo_t *s, int32_t deg)
{
  return servo_set_cdeg(s, (deg < 0 ? 0 : deg) * 100);
}
