#include "orient.h"

#include "imath.h"

void orient_from_accel(const int32_t a[3], orient_t *o)
{
  int64_t yz = (int64_t)a[1] * a[1] + (int64_t)a[2] * a[2];
  o->pitch_cdeg = iatan2_cdeg(-a[0], (int32_t)isqrt_u64((uint64_t)yz));
  o->roll_cdeg = iatan2_cdeg(a[1], a[2]);
}

int32_t orient_to_servo_deg(int32_t angle_cdeg)
{
  if (angle_cdeg < -9000) {
    angle_cdeg = -9000;
  }
  if (angle_cdeg > 9000) {
    angle_cdeg = 9000;
  }
  return 90 + idiv_round(angle_cdeg, 100);
}
