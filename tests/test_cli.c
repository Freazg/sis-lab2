/* Тести розбору команд консолі. */
#include "unity.h"

#include "cli.h"

void setUp(void) {}
void tearDown(void) {}

static void test_simple_commands_case_insensitive(void)
{
  TEST_ASSERT_EQUAL(CLI_HELP, cli_parse("help").id);
  TEST_ASSERT_EQUAL(CLI_SELFTEST, cli_parse("SelfTest").id);
  TEST_ASSERT_EQUAL(CLI_STATUS, cli_parse("  status  ").id);
  TEST_ASSERT_EQUAL(CLI_MODE_HOLD, cli_parse("mode HOLD").id);
  TEST_ASSERT_EQUAL(CLI_MODE_LIVE, cli_parse("mode\tlive").id);
}

static void test_capture_commands(void)
{
  TEST_ASSERT_EQUAL(CLI_CAP_I2C, cli_parse("cap i2c").id);
  TEST_ASSERT_EQUAL(CLI_CAP_SPI, cli_parse("cap spi").id);
  TEST_ASSERT_EQUAL(CLI_CAP_PWM, cli_parse("cap pwm").id);
  TEST_ASSERT_EQUAL(CLI_CAP_UART, cli_parse("cap uart").id);
  TEST_ASSERT_EQUAL(CLI_CAP_ANOMALY, cli_parse("cap anomaly").id);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("cap adc").id);
}

static void test_servo_command(void)
{
  cli_cmd_t c = cli_parse("servo tilt 135");
  TEST_ASSERT_EQUAL(CLI_SERVO, c.id);
  TEST_ASSERT_EQUAL(1, c.servo_index);
  TEST_ASSERT_EQUAL_INT32(135, c.value);
  TEST_ASSERT_EQUAL(0, cli_parse("servo pan 0").servo_index);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("servo pan 181").id);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("servo pan -1").id);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("servo pan 9x").id);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("servo roll 10").id);
}

static void test_errors_and_empty(void)
{
  TEST_ASSERT_EQUAL(CLI_NONE, cli_parse("").id);
  TEST_ASSERT_EQUAL(CLI_NONE, cli_parse("   ").id);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("reboot").id);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("help me").id);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("a b c d e").id); /* забагато токенів */
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("mode fast").id);
  TEST_ASSERT_EQUAL(CLI_ERROR, cli_parse("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx").id);
}

static void test_reader_assembles_lines(void)
{
  cli_reader_t r = { { 0 }, 0, 0, 0 };
  const char *in = "cap i2c\r\n";
  int done = 0;
  for (const char *p = in; *p; p++) {
    done += cli_feed(&r, *p);
  }
  TEST_ASSERT_EQUAL(1, done); /* "\r\n" — один рядок, а не два */
  TEST_ASSERT_EQUAL_STRING("cap i2c", r.buf);
}

static void test_reader_overflow_flagged(void)
{
  cli_reader_t r = { { 0 }, 0, 0, 0 };
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL(0, cli_feed(&r, 'a'));
  }
  TEST_ASSERT_EQUAL(1, r.overflow);
  TEST_ASSERT_EQUAL(1, cli_feed(&r, '\n'));
  TEST_ASSERT_EQUAL(CLI_LINE_MAX - 1, r.len);
  /* наступний символ починає новий рядок без ручного скидання */
  TEST_ASSERT_EQUAL(0, cli_feed(&r, 'h'));
  TEST_ASSERT_EQUAL(0, r.overflow);
  TEST_ASSERT_EQUAL(1, r.len);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_simple_commands_case_insensitive);
  RUN_TEST(test_capture_commands);
  RUN_TEST(test_servo_command);
  RUN_TEST(test_errors_and_empty);
  RUN_TEST(test_reader_assembles_lines);
  RUN_TEST(test_reader_overflow_flagged);
  return UNITY_END();
}
