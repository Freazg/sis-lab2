/* Тести фільтра руху: FR-02 (похибка в статиці), FR-05 (мертва зона, обмеження швидкості). */
#include <math.h>

#include "unity.h"

#include "motion.h"

void setUp(void) {}
void tearDown(void) {}

static void accel_for_angle(double pitch_deg, double roll_deg, int32_t a[3])
{
  double p = pitch_deg * M_PI / 180.0;
  double r = roll_deg * M_PI / 180.0;
  a[0] = (int32_t)lround(-1000.0 * sin(p));
  a[1] = (int32_t)lround(1000.0 * cos(p) * sin(r));
  a[2] = (int32_t)lround(1000.0 * cos(p) * cos(r));
}

static void test_static_error_within_fr02(void)
{
  static const double wedges[] = { 0, 15, 30, 45 }; /* PRD, таблиця D: клини 15°, 30°, 45° */
  for (unsigned i = 0; i < sizeof wedges / sizeof wedges[0]; i++) {
    motion_t m;
    int32_t a[3];
    const int32_t g[3] = { 0, 0, 0 };
    motion_reset(&m);
    accel_for_angle(wedges[i], 0, a);
    for (int k = 0; k < 400; k++) {
      motion_fuse(&m, a, g, 5000);
    }
    TEST_ASSERT_INT_WITHIN(200, (int32_t)(wedges[i] * 100), m.est.pitch_cdeg);
    TEST_ASSERT_INT_WITHIN(200, 0, m.est.roll_cdeg);
  }
}

static void test_first_sample_initialises_estimate(void)
{
  motion_t m;
  int32_t a[3];
  const int32_t g[3] = { 0, 0, 0 };
  motion_reset(&m);
  accel_for_angle(0, 40, a);
  motion_fuse(&m, a, g, 5000);
  TEST_ASSERT_INT_WITHIN(20, 4000, m.est.roll_cdeg); /* похибка iatan2 + округлення mg */
}

static void test_gyro_integrates_between_accel_corrections(void)
{
  /* 90 °/с по X протягом 100 мс при незмінному акселерометрі: швидка реакція на гіроскоп */
  motion_t m;
  int32_t a[3];
  const int32_t still[3] = { 0, 0, 0 };
  const int32_t turn[3] = { 90000, 0, 0 };
  motion_reset(&m);
  accel_for_angle(0, 0, a);
  motion_fuse(&m, a, still, 5000);
  for (int k = 0; k < 20; k++) {
    motion_fuse(&m, a, turn, 5000);
  }
  TEST_ASSERT_INT_WITHIN(150, 720, m.est.roll_cdeg); /* 9° мінус корекція акселерометром */
}

static void test_gyro_bias_is_subtracted(void)
{
  motion_t m;
  int32_t a[3];
  const int32_t g[3] = { 1000, -1000, 0 };
  motion_reset(&m);
  m.gyro_bias[0] = 1000;
  m.gyro_bias[1] = -1000;
  accel_for_angle(0, 0, a);
  for (int k = 0; k < 400; k++) {
    motion_fuse(&m, a, g, 5000);
  }
  TEST_ASSERT_INT_WITHIN(1, 0, m.est.roll_cdeg);
  TEST_ASSERT_INT_WITHIN(1, 0, m.est.pitch_cdeg);
}

static void test_long_gap_does_not_integrate_whole_pause(void)
{
  motion_t m;
  int32_t a[3];
  const int32_t turn[3] = { 100000, 0, 0 };
  motion_reset(&m);
  accel_for_angle(0, 0, a);
  motion_fuse(&m, a, turn, 5000);
  motion_fuse(&m, a, turn, 5000000); /* 5 с у довгій операції: без обмеження було б 500° */
  TEST_ASSERT_TRUE(m.est.roll_cdeg < 600);
}

static void test_zero_offset_applied(void)
{
  motion_t m;
  int32_t a[3];
  const int32_t g[3] = { 0, 0, 0 };
  motion_reset(&m);
  m.zero.roll_cdeg = 1000;
  accel_for_angle(0, 10, a);
  motion_fuse(&m, a, g, 5000);
  TEST_ASSERT_INT_WITHIN(30, 0, m.est.roll_cdeg);
}

static void test_deadzone_holds_small_error(void)
{
  TEST_ASSERT_EQUAL_INT32(9000, motion_slew(9000, 9150, 150, 600));
  TEST_ASSERT_EQUAL_INT32(9000, motion_slew(9000, 8850, 150, 600));
  TEST_ASSERT_EQUAL_INT32(9151, motion_slew(9000, 9151, 150, 600)); /* за межею — одразу до цілі */
}

static void test_rate_limit_clamps_step(void)
{
  TEST_ASSERT_EQUAL_INT32(9600, motion_slew(9000, 18000, 150, 600));
  TEST_ASSERT_EQUAL_INT32(8400, motion_slew(9000, 0, 150, 600));
  /* PRD D: стрибок 90° не швидше ніж за 300 мс = 15 кроків по 6° */
  int32_t a = 0;
  int steps = 0;
  while (a < 9000) {
    a = motion_slew(a, 9000, 150, 600);
    steps++;
  }
  TEST_ASSERT_EQUAL(15, steps);
}

static void test_servo_mapping_clamps(void)
{
  TEST_ASSERT_EQUAL_INT32(9000, motion_to_servo_cdeg(0));
  TEST_ASSERT_EQUAL_INT32(18000, motion_to_servo_cdeg(9900));
  TEST_ASSERT_EQUAL_INT32(0, motion_to_servo_cdeg(-9900));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_static_error_within_fr02);
  RUN_TEST(test_first_sample_initialises_estimate);
  RUN_TEST(test_gyro_integrates_between_accel_corrections);
  RUN_TEST(test_gyro_bias_is_subtracted);
  RUN_TEST(test_long_gap_does_not_integrate_whole_pause);
  RUN_TEST(test_zero_offset_applied);
  RUN_TEST(test_deadzone_holds_small_error);
  RUN_TEST(test_rate_limit_clamps_step);
  RUN_TEST(test_servo_mapping_clamps);
  return UNITY_END();
}
