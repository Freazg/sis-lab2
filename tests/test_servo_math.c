/* Тести перетворень: кут -> імпульс ШІМ, цілочисловий корінь і atan2, орієнтація з акселерометра. */
#include <math.h>

#include "unity.h"

#include "imath.h"
#include "orient.h"
#include "servo.h"

static uint32_t last_pulse;
static void pwm_set(void *ctx, uint32_t us) { (void)ctx; last_pulse = us; }
static uint32_t pwm_get(void *ctx) { (void)ctx; return last_pulse; }
static uint32_t pwm_period(void *ctx) { (void)ctx; return 20000; }
static const pwm_out_t pwm = { pwm_set, pwm_get, pwm_period, NULL };

void setUp(void) { last_pulse = 0; }
void tearDown(void) {}

static void test_pulse_endpoints_and_middle(void)
{
  TEST_ASSERT_EQUAL_UINT32(544, servo_pulse_for_deg(0, 544, 2400));
  TEST_ASSERT_EQUAL_UINT32(1472, servo_pulse_for_deg(90, 544, 2400));
  TEST_ASSERT_EQUAL_UINT32(2400, servo_pulse_for_deg(180, 544, 2400));
  TEST_ASSERT_EQUAL_UINT32(1008, servo_pulse_for_deg(45, 544, 2400));  /* значення з запису cap pwm */
  TEST_ASSERT_EQUAL_UINT32(1936, servo_pulse_for_deg(135, 544, 2400));
  TEST_ASSERT_EQUAL_UINT32(2400, servo_pulse_for_deg(270, 544, 2400)); /* насичення */
}

static void test_resolution_better_than_one_degree(void)
{
  /* FR-04: роздільність ≤ 1°: сусідні кути дають різні імпульси (крок ≈ 10.3 мкс > 1 тік) */
  for (uint32_t d = 0; d < 180; d++) {
    TEST_ASSERT_TRUE(servo_pulse_for_deg(d + 1, 544, 2400) > servo_pulse_for_deg(d, 544, 2400));
  }
}

static void test_set_deg_applies_limits(void)
{
  servo_t s;
  servo_init(&s, &pwm, 544, 2400);
  servo_set_limits(&s, 10, 170);
  TEST_ASSERT_EQUAL_UINT32(servo_pulse_for_deg(10, 544, 2400), servo_set_deg(&s, -30));
  TEST_ASSERT_EQUAL_UINT32(10, s.angle_deg);
  TEST_ASSERT_EQUAL_UINT32(servo_pulse_for_deg(170, 544, 2400), servo_set_deg(&s, 200));
  TEST_ASSERT_EQUAL_UINT32(last_pulse, pwm_get(NULL));
  servo_set_limits(&s, 200, 300); /* некоректні межі нормалізуються */
  TEST_ASSERT_EQUAL_UINT32(180, s.lim_hi_deg);
  TEST_ASSERT_EQUAL_UINT32(180, s.lim_lo_deg);
}

static void test_isqrt(void)
{
  TEST_ASSERT_EQUAL_UINT32(0, isqrt_u64(0));
  TEST_ASSERT_EQUAL_UINT32(1, isqrt_u64(3));
  TEST_ASSERT_EQUAL_UINT32(1000, isqrt_u64(1000000));
  TEST_ASSERT_EQUAL_UINT32(999, isqrt_u64(999999));
  TEST_ASSERT_EQUAL_UINT32(4294967295u, isqrt_u64(18446744065119617025ull));
}

static void test_atan2_against_libm(void)
{
  /* Похибка апроксимації < 0.25° у всіх квадрантах */
  for (int a = -179; a <= 180; a += 7) {
    double r = a * M_PI / 180.0;
    int32_t x = (int32_t)lround(cos(r) * 1000.0);
    int32_t y = (int32_t)lround(sin(r) * 1000.0);
    double ref = atan2((double)y, (double)x) * 18000.0 / M_PI;
    TEST_ASSERT_INT_WITHIN(25, (int32_t)lround(ref), iatan2_cdeg(y, x));
  }
  TEST_ASSERT_EQUAL_INT32(0, iatan2_cdeg(0, 0));
  TEST_ASSERT_EQUAL_INT32(9000, iatan2_cdeg(5, 0));
  TEST_ASSERT_EQUAL_INT32(-9000, iatan2_cdeg(-5, 0));
  TEST_ASSERT_EQUAL_INT32(18000, iatan2_cdeg(0, -5));
}

static void test_orientation_from_accel(void)
{
  orient_t o;
  const int32_t flat[3] = { 0, 0, 1000 };
  orient_from_accel(flat, &o);
  TEST_ASSERT_INT_WITHIN(1, 0, o.pitch_cdeg);
  TEST_ASSERT_INT_WITHIN(1, 0, o.roll_cdeg);

  const int32_t nose_down[3] = { -1000, 0, 0 }; /* вісь X вниз -> pitch +90° */
  orient_from_accel(nose_down, &o);
  TEST_ASSERT_INT_WITHIN(25, 9000, o.pitch_cdeg);

  const int32_t roll30[3] = { 0, 500, 866 };   /* крен 30° */
  orient_from_accel(roll30, &o);
  TEST_ASSERT_INT_WITHIN(25, 3000, o.roll_cdeg);
}

static void test_orientation_to_servo(void)
{
  TEST_ASSERT_EQUAL_INT32(90, orient_to_servo_deg(0));
  TEST_ASSERT_EQUAL_INT32(120, orient_to_servo_deg(3000));
  TEST_ASSERT_EQUAL_INT32(0, orient_to_servo_deg(-12000));  /* обмеження -90° */
  TEST_ASSERT_EQUAL_INT32(180, orient_to_servo_deg(9900));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_pulse_endpoints_and_middle);
  RUN_TEST(test_resolution_better_than_one_degree);
  RUN_TEST(test_set_deg_applies_limits);
  RUN_TEST(test_isqrt);
  RUN_TEST(test_atan2_against_libm);
  RUN_TEST(test_orientation_from_accel);
  RUN_TEST(test_orientation_to_servo);
  return UNITY_END();
}
