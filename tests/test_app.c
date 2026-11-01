/* Тести прикладного шару: самоперевірка (формат і вердикти), режими, команди консолі. */
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "app.h"
#include "app_internal.h"
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

/* Керований час: кожен виклик app_poll — dt мс, millis() сам не зростає. */
static void run_ms(uint32_t ms)
{
  fake_time_step_ms = 0;
  for (uint32_t t = 0; t < ms; t++) {
    fake_time_now_ms++;
    app_poll(&app);
  }
}

static void feed(const char *line)
{
  fake_con_input = line;
  fake_eol_pending = 1;
  app_poll(&app);
}

static void test_init_prints_ready_and_sets_live(void)
{
  app_init(&app, &f.hw);
  TEST_ASSERT_EQUAL(1, fake_con_count("READY"));
  TEST_ASSERT_EQUAL(1, fake_con_count("BOOT,fw=test,git=deadbeef,board=FAKE"));
  TEST_ASSERT_EQUAL(1, fake_con_count("TRAJ,session=0,pages=0,samples=0,torn=0"));
  TEST_ASSERT_EQUAL(APP_MODE_LIVE, app.mode);
  TEST_ASSERT_EQUAL(1, fake_leds[APP_LED_LIVE]);
  TEST_ASSERT_EQUAL(0, fake_leds[APP_LED_REC]);
  TEST_ASSERT_EQUAL_UINT32(10, f.pan.lim_lo_deg); /* FR-04: межі за замовчуванням */
  TEST_ASSERT_EQUAL_UINT32(170, f.tilt.lim_hi_deg);
}

static void test_selftest_command_keeps_limits(void)
{
  app_init(&app, &f.hw);
  fake_con_len = 0;
  feed("selftest\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("ST,dev=SUMMARY,res=PASS"));
  TEST_ASSERT_EQUAL_UINT32(10, f.pan.lim_lo_deg);
}

static void test_mode_cycle_and_leds(void)
{
  app_init(&app, &f.hw);
  run_ms(50);
  app_next_mode(&app);
  TEST_ASSERT_EQUAL(APP_MODE_HOLD, app.mode);
  TEST_ASSERT_EQUAL(0, fake_leds[APP_LED_LIVE]);
  TEST_ASSERT_EQUAL(0, fake_leds[APP_LED_REC]);
  app_next_mode(&app);
  TEST_ASSERT_EQUAL(APP_MODE_REC, app.mode);
  TEST_ASSERT_EQUAL(1, fake_leds[APP_LED_REC]);
  run_ms(100);
  app_next_mode(&app);
  TEST_ASSERT_EQUAL(APP_MODE_PLAY, app.mode);
  TEST_ASSERT_EQUAL(1, fake_leds[APP_LED_LIVE]);
  TEST_ASSERT_EQUAL(1, fake_leds[APP_LED_REC]);
  app_next_mode(&app);
  TEST_ASSERT_EQUAL(APP_MODE_LIVE, app.mode);
  TEST_ASSERT_EQUAL(1, fake_con_count("MODE,PLAY"));
}

static void test_play_without_trajectory_is_an_error(void)
{
  app_init(&app, &f.hw);
  feed("mode play\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("ERR,no_trajectory"));
  TEST_ASSERT_EQUAL(APP_MODE_LIVE, app.mode);
}

static void test_button_debounce_one_action_per_press(void)
{
  app_init(&app, &f.hw);
  run_ms(10);
  fake_button = 1;
  fake_btn_t = f.hw.cycles() - 480u; /* подія 10 мкс тому */
  fake_btn_pending = 1;
  run_ms(1);
  TEST_ASSERT_EQUAL(APP_MODE_HOLD, app.mode);
  fake_btn_pending = 1; /* брязкіт під час утримання */
  run_ms(30);
  TEST_ASSERT_EQUAL(APP_MODE_HOLD, app.mode);
  fake_button = 0;
  fake_btn_pending = 1; /* брязкіт відпускання */
  run_ms(10);
  TEST_ASSERT_EQUAL(APP_MODE_HOLD, app.mode);
  run_ms(15);
  TEST_ASSERT_EQUAL(0, fake_btn_pending); /* подія брязкоту скинута, а не виконана */
  fake_button = 1;
  fake_btn_t = f.hw.cycles();
  fake_btn_pending = 1;
  run_ms(1);
  TEST_ASSERT_EQUAL(APP_MODE_REC, app.mode);
  TEST_ASSERT_EQUAL_UINT32(2, app.btn_events);
  TEST_ASSERT_EQUAL_UINT32(2, app.st[APP_ST_BTN_LAT].count);
  TEST_ASSERT_EQUAL(2, fake_probe_edges[APP_PROBE_REACT]);
}

static void test_poll_executes_console_commands(void)
{
  app_init(&app, &f.hw);
  fake_con_len = 0;
  fake_con_input = "mode hold\r\nservo pan 180\nstatus\nbogus\n";
  app_poll(&app);
  TEST_ASSERT_EQUAL(1, fake_con_count("MODE,HOLD"));
  TEST_ASSERT_EQUAL(1, fake_con_count("SERVO,pan,deg=170,pulse_us=2297")); /* FR-04: межа 170° */
  TEST_ASSERT_EQUAL(1, fake_con_count("STATUS,mode=HOLD"));
  TEST_ASSERT_EQUAL(1, fake_con_count("ERR,unknown_command"));
}

static const uint8_t roll30[14] = { 0, 0, 0x20, 0x00, 0x37, 0x6D, 0xEF, 0x5C, 0, 0, 0, 0, 0, 0 };

static void test_live_mode_follows_tilt(void)
{
  /* крен +30°: ay=0.5 g, az=0.866 g -> серво pan = 90+30, не раніше ніж за 30°/300 °/с */
  app_init(&app, &f.hw);
  run_ms(50);
  fake_i2c_set_burst(&f.i2c_fake, roll30);
  run_ms(50);
  TEST_ASSERT_TRUE(f.pan.angle_cdeg > 9000);
  TEST_ASSERT_TRUE(f.pan.angle_cdeg < 12000); /* FR-05: обмеження швидкості */
  run_ms(1500);
  TEST_ASSERT_INT_WITHIN(200, 12000, f.pan.angle_cdeg); /* мертва зона + хвіст фільтра */
  TEST_ASSERT_INT_WITHIN(150, 9000, f.tilt.angle_cdeg);
  TEST_ASSERT_EQUAL_UINT32(1, app.st[APP_ST_E2E].count);
  TEST_ASSERT_TRUE(app.st[APP_ST_E2E].max / 48000u <= 25u); /* рух -> наступний запис CCR */
}

static void test_hold_freezes_servos(void)
{
  app_init(&app, &f.hw);
  feed("mode hold\n");
  fake_i2c_set_burst(&f.i2c_fake, roll30);
  run_ms(500);
  TEST_ASSERT_EQUAL_INT32(9000, f.pan.angle_cdeg);
}

static void test_imu_period_and_jitter_counters(void)
{
  app_init(&app, &f.hw);
  run_ms(1000);
  TEST_ASSERT_INT_WITHIN(1, 200, app.jit_imu.n);
  TEST_ASSERT_EQUAL_INT32(0, app.jit_imu.max_us);
  TEST_ASSERT_EQUAL_UINT32(0, app.jit_imu.missed);
  TEST_ASSERT_INT_WITHIN(1, 50, app.jit_servo.n);
}

static void test_stall_counts_as_missed_runs(void)
{
  app_init(&app, &f.hw);
  run_ms(100);
  uint32_t viol = app.jit_imu.viol;
  fake_time_now_ms += 100; /* довга операція в головному циклі */
  run_ms(20);
  TEST_ASSERT_EQUAL_UINT32(1, app.jit_imu.missed);
  TEST_ASSERT_TRUE(app.jit_imu.max_us >= 90000);
  TEST_ASSERT_EQUAL_UINT32(viol + 1u, app.jit_imu.viol);
}

static void test_imu_lost_and_restored(void)
{
  app_init(&app, &f.hw);
  run_ms(50);
  f.i2c_fake.dev_addr = 0x50;
  run_ms(90);
  TEST_ASSERT_EQUAL(0, app.imu_lost); /* FR-10: менше 100 мс — ще не втрата */
  run_ms(30);
  TEST_ASSERT_EQUAL(1, app.imu_lost);
  TEST_ASSERT_EQUAL(1, fake_con_count("ERR,IMU lost"));
  TEST_ASSERT_TRUE(fake_recovered >= 1);
  int32_t pan = f.pan.angle_cdeg;
  fake_i2c_set_burst(&f.i2c_fake, roll30);
  run_ms(300);
  TEST_ASSERT_EQUAL_INT32(pan, f.pan.angle_cdeg); /* серво в останньому положенні */
  TEST_ASSERT_TRUE(fake_recovered >= 3);          /* контролер скидається періодично */
  f.i2c_fake.dev_addr = 0x68;
  run_ms(20);
  TEST_ASSERT_EQUAL(0, app.imu_lost);
  TEST_ASSERT_EQUAL(1, fake_con_count("IMU,restored"));
}

static void test_sensor_absent_at_boot_recovers_later(void)
{
  f.i2c_fake.dev_addr = 0x50;
  app_init(&app, &f.hw);
  TEST_ASSERT_EQUAL(1, app.imu_lost);
  run_ms(20);
  f.i2c_fake.dev_addr = 0x68;
  run_ms(20);
  TEST_ASSERT_EQUAL(0, app.imu_lost);
  TEST_ASSERT_EQUAL(1, fake_con_count("BOOT,first_imu_us="));
}

/* Запис -> вимкнення живлення (новий app_init) -> відтворення (FR-07, FR-08) */
static void record_ms(uint32_t ms)
{
  feed("mode rec\n");
  run_ms(ms);
  feed("mode hold\n");
}

static void test_record_survives_reset_and_replays(void)
{
  app_init(&app, &f.hw);
  run_ms(50);
  fake_i2c_set_burst(&f.i2c_fake, roll30);
  record_ms(6990); /* 350 семплів: 3 сторінки */
  TEST_ASSERT_EQUAL(1, fake_con_count("REC,DONE,session=1,samples=350,pages=3"));
  int32_t last_pan = f.pan.angle_cdeg;

  fake_con_len = 0;
  fake_con[0] = '\0';
  app_init(&app, &f.hw);
  TEST_ASSERT_EQUAL(1, fake_con_count("TRAJ,session=1,pages=3,samples=350,torn=0"));
  feed("mode play\n");
  run_ms(8000);
  TEST_ASSERT_EQUAL(1, fake_con_count("PLAY,DONE,session=1,samples=350"));
  TEST_ASSERT_EQUAL(APP_MODE_HOLD, app.mode);
  TEST_ASSERT_EQUAL_INT32(last_pan / 10, f.pan.angle_cdeg / 10);
}

static void test_dump_prints_all_samples(void)
{
  app_init(&app, &f.hw);
  record_ms(990);
  fake_con_len = 0;
  fake_con[0] = '\0';
  feed("dump\n");
  TEST_ASSERT_EQUAL(50, fake_con_count("DUMP,") - 1); /* + рядок END */
  TEST_ASSERT_EQUAL(1, fake_con_count("DUMP,END,session=1,samples=50,pages=1,crc=OK"));
}

static void test_long_commands_refused_while_recording(void)
{
  app_init(&app, &f.hw);
  feed("mode rec\n");
  feed("dump\n");
  feed("erase\n");
  TEST_ASSERT_EQUAL(2, fake_con_count("ERR,busy"));
}

static void test_erase_clears_trajectory(void)
{
  app_init(&app, &f.hw);
  record_ms(500);
  feed("erase\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("ERASE,DONE,sectors=16"));
  feed("mode play\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("ERR,no_trajectory"));
}

static void test_second_recording_gets_new_session(void)
{
  app_init(&app, &f.hw);
  record_ms(200);
  record_ms(200);
  TEST_ASSERT_EQUAL(1, fake_con_count("REC,DONE,session=2"));
}

static void test_torn_page_is_detected_and_dropped(void)
{
  app_init(&app, &f.hw);
  feed("mode rec\n");
  run_ms(3000);          /* 150 семплів: сторінка 0 записана, 1 у буфері */
  feed("fault powerloss\n");
  run_ms(2500);          /* наступне закриття сторінки обривається скиданням */
  TEST_ASSERT_EQUAL(1, fake_resets);
  fake_con_len = 0;
  fake_con[0] = '\0';
  app.mode = APP_MODE_HOLD; /* на платі після скидання стан починається заново */
  app_init(&app, &f.hw);
  TEST_ASSERT_EQUAL(1, fake_con_count("TRAJ,session=1,pages=1,samples=122,torn=1"));
}

static void test_recording_stops_when_area_is_full(void)
{
  app_init(&app, &f.hw);
  feed("mode rec\n");
  run_ms(630000); /* 256 сторінок по 2.44 с = 624 с */
  TEST_ASSERT_EQUAL(APP_MODE_HOLD, app.mode);
  TEST_ASSERT_EQUAL(1, fake_con_count("REC,DONE,session=1"));
  TEST_ASSERT_EQUAL_UINT32(APP_TRAJ_SECTORS * 16u, app.traj_seq);
}

static void test_stats_format_matches_methodology(void)
{
  app_init(&app, &f.hw);
  run_ms(100);
  fake_con_len = 0;
  fake_con[0] = '\0';
  feed("stats\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("STAT name=imu n="));
  TEST_ASSERT_EQUAL(1, fake_con_count("STAT name=btn_latency n=0 min_us=0 avg_us=0 max_us=0"));
  TEST_ASSERT_EQUAL(1, fake_con_count("JITTER name=imu period_us=5000"));
  TEST_ASSERT_EQUAL(1, fake_con_count("LOAD pct="));
  TEST_ASSERT_EQUAL(1, fake_con_count("CNT rx_dropped=0 imu_err=0 btn=0"));
  feed("stats reset\n");
  TEST_ASSERT_EQUAL_UINT32(0, app.st[APP_ST_IMU].count);
}

static void test_load_excludes_idle_passes(void)
{
  app_init(&app, &f.hw);
  run_ms(2500);
  TEST_ASSERT_TRUE(app.load_max < 100); /* час не йде: завантаження не 100 %, хоч цикл крутиться */
}

static void test_cmd_latency_measured_for_status(void)
{
  app_init(&app, &f.hw);
  feed("status\n");
  TEST_ASSERT_EQUAL_UINT32(1, app.st[APP_ST_CMD_LAT].count);
  TEST_ASSERT_EQUAL(1, fake_con_count("imu=OK"));
}

static void test_cal_sets_zero_and_gyro_bias(void)
{
  static const uint8_t biased[14] = { 0, 0, 0x20, 0x00, 0x37, 0x6D, 0xEF, 0x5C, 0x00, 0x83, 0, 0, 0, 0 };
  app_init(&app, &f.hw);
  fake_i2c_set_burst(&f.i2c_fake, biased); /* 131 LSB = 1 °/с по X */
  feed("cal\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("CAL,start"));
  run_ms(1100);
  TEST_ASSERT_EQUAL(1, fake_con_count("CAL,done,"));
  TEST_ASSERT_INT_WITHIN(10, 1000, app.motion.gyro_bias[0]);
  run_ms(1000);
  TEST_ASSERT_INT_WITHIN(20, 0, app.motion.est.roll_cdeg); /* нахил стенда став нулем */
}

static void test_limits_and_rate_commands(void)
{
  app_init(&app, &f.hw);
  feed("limits 30 150\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("LIMITS,min=30,max=150"));
  TEST_ASSERT_EQUAL_UINT32(30, f.tilt.lim_lo_deg);
  feed("limits 150 30\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("ERR,limits"));
  feed("rate 400\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("RATE,hz=400,period_us=2500"));
  TEST_ASSERT_EQUAL_UINT32(2500, app.jit_imu.period_us);
  feed("rate 5\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("ERR,rate"));
}

static void test_diagnostic_commands(void)
{
  app_init(&app, &f.hw);
  run_ms(20);
  feed("mem\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("STACK used=800 reserved=1024"));
  feed("hist\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("HIST name=imu bin_us=100"));
  feed("evt\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("EVT t_us=0 name=boot"));
  feed("perfcal\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("PERFCAL loop500_cyc=1750"));
  TEST_ASSERT_EQUAL(1, fake_con_count("backward=0"));
  feed("help\n");
  TEST_ASSERT_EQUAL(1, fake_con_count("cmds:"));
}

static void test_reset_command(void)
{
  app_init(&app, &f.hw);
  feed("reset\n");
  TEST_ASSERT_EQUAL(1, fake_resets);
}

static void test_event_log_keeps_last_entries(void)
{
  app_init(&app, &f.hw);
  for (int i = 0; i < 40; i++) {
    app_evt(&app, APP_EV_MODE);
  }
  fake_con_len = 0;
  fake_con[0] = '\0';
  feed("evt\n");
  TEST_ASSERT_EQUAL((int)APP_EVT_LOG, fake_con_count("EVT t_us="));
  TEST_ASSERT_EQUAL(0, fake_con_count("name=boot"));
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
  RUN_TEST(test_selftest_command_keeps_limits);
  RUN_TEST(test_mode_cycle_and_leds);
  RUN_TEST(test_play_without_trajectory_is_an_error);
  RUN_TEST(test_button_debounce_one_action_per_press);
  RUN_TEST(test_poll_executes_console_commands);
  RUN_TEST(test_live_mode_follows_tilt);
  RUN_TEST(test_hold_freezes_servos);
  RUN_TEST(test_imu_period_and_jitter_counters);
  RUN_TEST(test_stall_counts_as_missed_runs);
  RUN_TEST(test_imu_lost_and_restored);
  RUN_TEST(test_sensor_absent_at_boot_recovers_later);
  RUN_TEST(test_record_survives_reset_and_replays);
  RUN_TEST(test_dump_prints_all_samples);
  RUN_TEST(test_long_commands_refused_while_recording);
  RUN_TEST(test_erase_clears_trajectory);
  RUN_TEST(test_second_recording_gets_new_session);
  RUN_TEST(test_torn_page_is_detected_and_dropped);
  RUN_TEST(test_recording_stops_when_area_is_full);
  RUN_TEST(test_stats_format_matches_methodology);
  RUN_TEST(test_load_excludes_idle_passes);
  RUN_TEST(test_cmd_latency_measured_for_status);
  RUN_TEST(test_cal_sets_zero_and_gyro_bias);
  RUN_TEST(test_limits_and_rate_commands);
  RUN_TEST(test_diagnostic_commands);
  RUN_TEST(test_reset_command);
  RUN_TEST(test_event_log_keeps_last_entries);
  RUN_TEST(test_capture_commands_are_framed);
  RUN_TEST(test_capture_i2c_prints_bus_bytes);
  RUN_TEST(test_capture_anomaly_reports_and_recovers);
  return UNITY_END();
}
