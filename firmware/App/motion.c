#include "motion.h"

#include <string.h>

void motion_reset(motion_t *m) { memset(m, 0, sizeof *m); }

static int32_t blend(int32_t predicted, int32_t measured)
{
  int64_t v = (int64_t)predicted * MOTION_ALPHA_PERMILLE + (int64_t)measured * (1000 - MOTION_ALPHA_PERMILLE);
  return (int32_t)(v / 1000);
}

void motion_fuse(motion_t *m, const int32_t accel_mg[3], const int32_t gyro_mdps[3], uint32_t dt_us)
{
  orient_t acc;
  orient_from_accel(accel_mg, &acc);
  acc.pitch_cdeg -= m->zero.pitch_cdeg;
  acc.roll_cdeg -= m->zero.roll_cdeg;
  if (!m->started) {
    m->est = acc;
    m->started = 1;
    return;
  }
  if (dt_us > MOTION_DT_MAX_US) {
    dt_us = MOTION_DT_MAX_US;
  }
  /* мілі-°/с × мкс -> соті градуса: ділення на 10^7 */
  int64_t droll = ((int64_t)(gyro_mdps[0] - m->gyro_bias[0]) * dt_us) / 10000000;
  int64_t dpitch = ((int64_t)(gyro_mdps[1] - m->gyro_bias[1]) * dt_us) / 10000000;
  m->est.roll_cdeg = blend(m->est.roll_cdeg + (int32_t)droll, acc.roll_cdeg);
  m->est.pitch_cdeg = blend(m->est.pitch_cdeg + (int32_t)dpitch, acc.pitch_cdeg);
}

int32_t motion_slew(int32_t current_cdeg, int32_t target_cdeg, int32_t deadzone_cdeg, int32_t max_step_cdeg)
{
  int32_t err = target_cdeg - current_cdeg;
  if (err <= deadzone_cdeg && err >= -deadzone_cdeg) {
    return current_cdeg;
  }
  if (err > max_step_cdeg) {
    err = max_step_cdeg;
  } else if (err < -max_step_cdeg) {
    err = -max_step_cdeg;
  }
  return current_cdeg + err;
}

int32_t motion_to_servo_cdeg(int32_t angle_cdeg)
{
  if (angle_cdeg < -9000) {
    angle_cdeg = -9000;
  }
  if (angle_cdeg > 9000) {
    angle_cdeg = 9000;
  }
  return 9000 + angle_cdeg;
}
