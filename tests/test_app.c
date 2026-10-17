/* Тести прикладного шару: самоперевірка (формат і вердикти), режими, команди консолі. */
#include <string.h>

#include "unity.h"

#include "app.h"
#include "fake_hw.h"
#include "fake_time.h"
#include "selftest.h"

static fake_hw_t f;
static app_t app;

void setUp(void) { fake_hw_init(&f); }
void tearDown(void) {}

/* Кожен рядок самоперевірки має вигляд ST,dev=...,res=PASS|FAIL[,k=v]... без пробілів */
static void assert_selftest_lines_parseable(void)
{
  char copy[sizeof fake_con];
  strcpy(copy, fake_con);
  int st_lines = 0;
  for (char *line = strtok(copy, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    if (strncmp(line, "ST,", 3) != 0) {
      continue;
    }
    st_lines++;
    TEST_ASSERT_NULL_MESSAGE(strchr(line, ' '), line);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, ",dev="), line);
    if (strstr(line, "dev=BEGIN") == NULL) {
      TEST_ASSERT_TRUE_MESSAGE(strstr(line, ",res=PASS") || strstr(line, ",res=FAIL"), line);
    }
  }
  TEST_ASSERT_EQUAL(11, st_lines); /* BEGIN + 9 пристроїв + SUMMARY */
}

static void test_selftest_all_pass(void)
{
  selftest_result_t r = selftest_run(&f.hw);
  TEST_ASSERT_EQUAL(9, r.pass);
  TEST_ASSERT_EQUAL(0, r.fail);
  TEST_ASSERT_EQUAL(1, fake_con_count("ST,dev=SUMMARY,res=PASS,pass=9,fail=0"));
  TEST_ASSERT_EQUAL(1, fake_con_count("dev=MPU6050,res=PASS"));
  TEST_ASSERT_EQUAL(1, fake_con_count("norm_mg=1000,dev_mg=0"));
  TEST_ASSERT_EQUAL(1, fake_con_count("dev=W25Q64,res=PASS,bus=SPI1,jedec=EF4017"));
  TEST_ASSERT_EQUAL(2, fake_con_count("pulse_us_0deg=544,pulse_us_180deg=2400,pulse_us_90deg=1472")); /* pan і tilt */
  TEST_ASSERT_NOT_NULL(strstr(fake_aux, "FT232RL link check"));
  assert_selftest_lines_parseable();
}

static void test_selftest_mpu_absent(void)
{
  f.i2c_fake.dev_addr = 0x50; /* на 0x68 ніхто не відповідає */
  selftest_result_t r = selftest_run(&f.hw);
  TEST_ASSERT_EQUAL(1, r.fail);
  TEST_ASSERT_EQUAL(1, fake_con_count("dev=MPU6050,res=FAIL,bus=I2C1,addr=0x68,who=0x00,err=NACK"));
  assert_selftest_lines_parseable();
}

static void test_selftest_mpu_implausible_acceleration(void)
{
  /* 1.2 g у спокої (raw Z = 0x4CCD = 19661) — поза допуском 1000 ± 110 mg */
  static const uint8_t heavy[14] = { 0, 0, 0, 0, 0x4C, 0xCD, 0xEF, 0x5C, 0, 0, 0, 0, 0, 0 };
  fake_i2c_set_burst(&f.i2c_fake, heavy);
  selftest_result_t r = selftest_run(&f.hw);
  TEST_ASSERT_EQUAL(1, r.fail);
  TEST_ASSERT_EQUAL(1, fake_con_count("norm_mg=1200,dev_mg=200"));
  TEST_ASSERT_EQUAL(1, fake_con_count("chk=aGT")); /* a — прискорення, G/T — гіроскоп і температура в нормі */
}

static void test_selftest_mpu_gyro_and_temperature_out_of_range(void)
{
  /* гіроскоп X = +30 °/с (3930 LSB), температура 60 °C (raw = (60-36.53)*340 = 7980 = 0x1F2C) */
  static const uint8_t bad[14] = { 0, 0, 0, 0, 0x40, 0x00, 0x1F, 0x2C, 0x0F, 0x5A, 0, 0, 0, 0 };
  fake_i2c_set_burst(&f.i2c_fake, bad);
  selftest_result_t r = selftest_run(&f.hw);
  TEST_ASSERT_EQUAL(1, r.fail);
  TEST_ASSERT_EQUAL(1, fake_con_count("chk=Agt"));
}

static void test_selftest_flash_bit_error(void)
{
  f.flash_fake.corrupt_bit = 3;
  selftest_result_t r = selftest_run(&f.hw);
  TEST_ASSERT_EQUAL(1, r.fail);
  TEST_ASSERT_EQUAL(1, fake_con_count("dev=W25Q64,res=FAIL"));
  TEST_ASSERT_EQUAL(1, fake_con_count("stage=blank,err=VERIFY")); /* перший байт після стирання != FF */
}

static void test_selftest_flash_wrong_id(void)
{
  f.flash_fake.jedec[0] = 0xC2;
  selftest_result_t r = selftest_run(&f.hw);
  TEST_ASSERT_EQUAL(1, r.fail);
  TEST_ASSERT_EQUAL(1, fake_con_count("jedec=C24017"));
  TEST_ASSERT_EQUAL(1, fake_con_count("stage=jedec,err=BAD_ID"));
}

static void test_selftest_led_and_button_and_aux_faults(void)
{
  fake_led_stuck[APP_LED_REC] = 1;
  fake_button = 1;
  fake_aux_fail = 1;
  selftest_result_t r = selftest_run(&f.hw);
  TEST_ASSERT_EQUAL(3, r.fail);
  TEST_ASSERT_EQUAL(1, fake_con_count("dev=LED_REC,res=FAIL"));
  TEST_ASSERT_EQUAL(1, fake_con_count("dev=BTN_B1,res=FAIL,pin=PC13,idle=pressed"));
  TEST_ASSERT_EQUAL(1, fake_con_count("dev=FT232RL,res=FAIL,uart=USART1,baud=115200,tx=TIMEOUT"));
}

static void test_selftest_wrong_clock(void)
{
  f.hw.sysclk_hz = 12000000u; /* HSI48/4 — значення після скидання, якщо SystemClock_Config не спрацював */
  selftest_result_t r = selftest_run(&f.hw);
  TEST_ASSERT_EQUAL(1, r.fail);
  TEST_ASSERT_EQUAL(1, fake_con_count("dev=MCU,res=FAIL"));
}

static void test_init_prints_ready_and_sets_live(void)
{
  app_init(&app, &f.hw);
  TEST_ASSERT_EQUAL(1, fake_con_count("READY"));
  TEST_ASSERT_EQUAL(APP_MODE_LIVE, app.mode);
  TEST_ASSERT_EQUAL(1, fake_leds[APP_LED_LIVE]);
  TEST_ASSERT_EQUAL(0, fake_leds[APP_LED_REC]);
}

static void test_button_edge_toggles_mode_once(void)
{
  app_init(&app, &f.hw);
  app_on_button(&app, 1);
  app_on_button(&app, 1); /* утримання — без повторного перемикання */
  TEST_ASSERT_EQUAL(APP_MODE_HOLD, app.mode);
  app_on_button(&app, 0);
  app_on_button(&app, 1);
  TEST_ASSERT_EQUAL(APP_MODE_LIVE, app.mode);
}

static void test_poll_executes_console_commands(void)
{
  app_init(&app, &f.hw);
  fake_con_len = 0;
  fake_con_input = "mode hold\r\nservo pan 180\nstatus\nbogus\n";
  app_poll(&app);
  TEST_ASSERT_EQUAL(1, fake_con_count("MODE,HOLD"));
  TEST_ASSERT_EQUAL(1, fake_con_count("SERVO,pan,deg=180,pulse_us=2400"));
  TEST_ASSERT_EQUAL(1, fake_con_count("STATUS,mode=HOLD"));
  TEST_ASSERT_EQUAL(1, fake_con_count("ERR,unknown_command"));
}

static void test_live_mode_follows_tilt(void)
{
  /* крен +30°: ay=0.5 g, az=0.866 g -> серво pan = 90+30 */
  static const uint8_t roll30[14] = { 0, 0, 0x20, 0x00, 0x37, 0x6D, 0xEF, 0x5C, 0, 0, 0, 0, 0, 0 };
  app_init(&app, &f.hw);
  fake_i2c_set_burst(&f.i2c_fake, roll30);
  fake_time_now_ms += 100;
  app_poll(&app);
  TEST_ASSERT_INT_WITHIN(1, 120, app.pan_deg);
  TEST_ASSERT_INT_WITHIN(1, 90, app.tilt_deg);
}

static void test_capture_commands_are_framed(void)
{
  static const char *const cmds[] = { "cap i2c\n", "cap spi\n", "cap pwm\n", "cap uart\n", "cap anomaly\n" };
  static const char *const names[] = { "i2c", "spi", "pwm", "uart", "anomaly" };
  app_init(&app, &f.hw);
  for (int i = 0; i < 5; i++) {
    char b[32];
    fake_con_len = 0;
    fake_con_input = cmds[i];
    app_poll(&app);
    snprintf(b, sizeof b, "CAP,%s,BEGIN", names[i]);
    TEST_ASSERT_EQUAL_MESSAGE(1, fake_con_count(b), cmds[i]);
    snprintf(b, sizeof b, "CAP,%s,END", names[i]);
    TEST_ASSERT_EQUAL_MESSAGE(1, fake_con_count(b), cmds[i]);
  }
}

static void test_capture_i2c_prints_bus_bytes(void)
{
  app_init(&app, &f.hw);
  fake_con_len = 0;
  fake_con_input = "cap i2c\n";
  app_poll(&app);
  TEST_ASSERT_EQUAL(1, fake_con_count("CAP,i2c,who_am_i=0x68,st=OK"));
  TEST_ASSERT_EQUAL(1, fake_con_count("CAP,i2c,burst=000000004000EF5C000000000000,st=OK"));
  TEST_ASSERT_EQUAL(1, fake_con_count("CAP,i2c,acc_mg=0/0/1000,temp_cdeg=2400"));
}

static void test_capture_anomaly_reports_and_recovers(void)
{
  app_init(&app, &f.hw);
  fake_con_len = 0;
  fake_con_input = "cap anomaly\n";
  app_poll(&app);
  TEST_ASSERT_EQUAL(1, fake_con_count("step=wrong_addr,addr=0x69,st=NACK"));
  TEST_ASSERT_EQUAL(1, fake_con_count("step=mem_read_restart,val=0x00,st=TIMEOUT"));
  TEST_ASSERT_EQUAL(1, fake_con_count("step=read_after_recover,val=0x68,st=OK"));
  TEST_ASSERT_EQUAL(1, fake_recovered);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_selftest_all_pass);
  RUN_TEST(test_selftest_mpu_absent);
  RUN_TEST(test_selftest_mpu_implausible_acceleration);
  RUN_TEST(test_selftest_mpu_gyro_and_temperature_out_of_range);
  RUN_TEST(test_selftest_flash_bit_error);
  RUN_TEST(test_selftest_flash_wrong_id);
  RUN_TEST(test_selftest_led_and_button_and_aux_faults);
  RUN_TEST(test_selftest_wrong_clock);
  RUN_TEST(test_init_prints_ready_and_sets_live);
  RUN_TEST(test_button_edge_toggles_mode_once);
  RUN_TEST(test_poll_executes_console_commands);
  RUN_TEST(test_live_mode_follows_tilt);
  RUN_TEST(test_capture_commands_are_framed);
  RUN_TEST(test_capture_i2c_prints_bus_bytes);
  RUN_TEST(test_capture_anomaly_reports_and_recovers);
  return UNITY_END();
}
