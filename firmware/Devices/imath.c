#include "imath.h"

uint32_t isqrt_u64(uint64_t v)
{
  uint64_t res = 0;
  uint64_t bit = (uint64_t)1 << 62;
  while (bit > v) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (v >= res + bit) {
      v -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return (uint32_t)res;
}

/* atan(z) для z = num/den, 0 <= num <= den, результат у сотих градуса (0..4500). */
static int32_t atan_unit_cdeg(int64_t num, int64_t den)
{
  if (den == 0) {
    return 0;
  }
  /* z у Q16 */
  int64_t z = (num << 16) / den;
  /* rad(Q16) = π/4·z + 0.273·z·(1−z);  π/4 = 0.785398 */
  int64_t one = 1 << 16;
  int64_t t1 = (51472 * z) >> 16;                       /* 0.785398 * 65536 = 51472 */
  int64_t t2 = (((17891 * z) >> 16) * (one - z)) >> 16; /* 0.273 * 65536 = 17891 */
  int64_t rad_q16 = t1 + t2;
  /* градуси*100 = rad * 18000/π = rad * 5729.578 */
  return (int32_t)((rad_q16 * 5729578 / 1000 + (1 << 15)) >> 16);
}

int32_t iatan2_cdeg(int32_t y, int32_t x)
{
  if (x == 0 && y == 0) {
    return 0;
  }
  int64_t ax = x < 0 ? -(int64_t)x : x;
  int64_t ay = y < 0 ? -(int64_t)y : y;
  int32_t a = (ay <= ax) ? atan_unit_cdeg(ay, ax) : 9000 - atan_unit_cdeg(ax, ay);
  if (x < 0) {
    a = 18000 - a;
  }
  return (y < 0) ? -a : a;
}
