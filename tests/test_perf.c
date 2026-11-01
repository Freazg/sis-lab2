/* Тести вимірювального модуля і форматера рядків. */
#include <limits.h>
#include <string.h>

#include "unity.h"

#include "app_io.h"
#include "perf.h"

void setUp(void) {}
void tearDown(void) {}

static void test_stat_min_avg_max(void)
{
  perf_stat_t s = { 0, 0, 0, 0 };
  TEST_ASSERT_EQUAL_UINT32(0, perf_avg(&s));
  perf_add(&s, 300);
  perf_add(&s, 100);
  perf_add(&s, 200);
  TEST_ASSERT_EQUAL_UINT32(3, s.count);
  TEST_ASSERT_EQUAL_UINT32(100, s.min);
  TEST_ASSERT_EQUAL_UINT32(300, s.max);
  TEST_ASSERT_EQUAL_UINT32(200, perf_avg(&s));
}

static void test_stat_sum_does_not_overflow_32_bits(void)
{
  perf_stat_t s = { 0, 0, 0, 0 };
  for (int i = 0; i < 4; i++) {
    perf_add(&s, 0xFFFFFFF0u);
  }
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFF0u, perf_avg(&s));
}

/* Методичка, контрольне питання 4: різниця беззнакових коректна після одного переповнення */
static void test_cycle_difference_across_wrap(void)
{
  uint32_t t0 = 0xFFFFFF00u;
  uint32_t t1 = 0x00000100u;
  TEST_ASSERT_EQUAL_UINT32(0x200u, t1 - t0);
}

static void test_jitter_signed_range_missed_and_violations(void)
{
  perf_jitter_t j;
  perf_jitter_reset(&j, 5000, 500);
  perf_jitter_add(&j, 5000);
  perf_jitter_add(&j, 4900);
  perf_jitter_add(&j, 5600);
  perf_jitter_add(&j, 10100);
  TEST_ASSERT_EQUAL_INT32(-100, j.min_us);
  TEST_ASSERT_EQUAL_INT32(5100, j.max_us);
  TEST_ASSERT_EQUAL_UINT32(4, j.n);
  TEST_ASSERT_EQUAL_UINT32(1, j.missed);
  TEST_ASSERT_EQUAL_UINT32(2, j.viol);
  TEST_ASSERT_EQUAL_UINT32(1, j.hist[0]);  /* 0 мкс */
  TEST_ASSERT_EQUAL_UINT32(1, j.hist[1]);  /* 100 мкс: межа кошика */
  TEST_ASSERT_EQUAL_UINT32(1, j.hist[PERF_HIST_BINS - 1]);
}

static void test_percentile_from_histogram(void)
{
  perf_jitter_t j;
  perf_jitter_reset(&j, 5000, 500);
  for (int i = 0; i < 99; i++) {
    perf_jitter_add(&j, 5050); /* кошик 0 */
  }
  perf_jitter_add(&j, 5350);   /* кошик 3 */
  TEST_ASSERT_EQUAL_UINT32(100, perf_jitter_percentile_us(&j, 99));
  TEST_ASSERT_EQUAL_UINT32(400, perf_jitter_percentile_us(&j, 100));
}

static void test_format_integers(void)
{
  char b[64];
  app_format(b, sizeof b, "%d %ld %u %lu", -12, -3000000L, 7u, 4000000000UL);
  TEST_ASSERT_EQUAL_STRING("-12 -3000000 7 4000000000", b);
  app_format(b, sizeof b, "%ld", LONG_MIN);
  TEST_ASSERT_EQUAL_STRING(sizeof(long) == 4 ? "-2147483648" : "-9223372036854775808", b);
}

static void test_format_hex_width_and_strings(void)
{
  char b[64];
  app_format(b, sizeof b, "0x%02X %06lX %x %c%c %s %3d|%03d|%%", 0x68, 0x7FF000UL, 0xABu, 'A', 'g', "ok", 5, -5);
  TEST_ASSERT_EQUAL_STRING("0x68 7FF000 ab Ag ok   5|-05|%", b);
}

static void test_format_truncates_and_reports_length(void)
{
  char b[8];
  int n = app_format(b, sizeof b, "%s", "0123456789");
  TEST_ASSERT_EQUAL(10, n);
  TEST_ASSERT_EQUAL_STRING("0123456", b);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_stat_min_avg_max);
  RUN_TEST(test_stat_sum_does_not_overflow_32_bits);
  RUN_TEST(test_cycle_difference_across_wrap);
  RUN_TEST(test_jitter_signed_range_missed_and_violations);
  RUN_TEST(test_percentile_from_histogram);
  RUN_TEST(test_format_integers);
  RUN_TEST(test_format_hex_width_and_strings);
  RUN_TEST(test_format_truncates_and_reports_length);
  return UNITY_END();
}
