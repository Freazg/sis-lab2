#include "app.h"

#include "app_internal.h"
#include "app_io.h"
#include "imath.h"
#include "selftest.h"

#define IMU_RECOVER_MS 100u /* як часто скидати контролер I2C, поки сенсор втрачений */
#define MOTION_STEP_MG 100 /* стрибок прискорення між семплами = «рух IMU виявлено» (FR-03, FR-11) */

/* ---- Режими -------------------------------------------------------------- */
const char *app_mode_name(app_mode_t m)
{
  static const char *const names[] = { "LIVE", "HOLD", "REC", "PLAY" };
  return names[m];
}

void app_set_mode(app_t *a, app_mode_t m)
{
  const app_hw_t *hw = a->hw;
  if (a->mode == APP_MODE_REC) {
    app_rec_end(a);
  } else if (a->mode == APP_MODE_PLAY) {
    app_play_end(a);
  }
  if (m == APP_MODE_REC && !app_rec_begin(a)) {
    m = APP_MODE_HOLD;
  } else if (m == APP_MODE_PLAY && !app_play_begin(a)) {
    app_print(hw, "ERR,no_trajectory\r\n");
    m = APP_MODE_LIVE;
  }
  a->mode = m;
  /* FR-06: LIVE — зелений, REC — червоний, PLAY — обидва, HOLD — жоден */
  hw->led_set(APP_LED_LIVE, m == APP_MODE_LIVE || m == APP_MODE_PLAY);
  hw->led_set(APP_LED_REC, m == APP_MODE_REC || m == APP_MODE_PLAY);
  app_evt(a, APP_EV_MODE);
  app_print(hw, "MODE,%s\r\n", app_mode_name(m));
}

void app_next_mode(app_t *a)
{
  static const app_mode_t next[] = { APP_MODE_HOLD, APP_MODE_REC, APP_MODE_PLAY, APP_MODE_LIVE };
  app_set_mode(a, next[a->mode]);
}

/* ---- Задачі -------------------------------------------------------------- */
static bool due(uint32_t now, uint32_t next) { return (int32_t)(now - next) >= 0; }

/** Абсолютний розклад: наступний запуск = попередній + період, після пропуску — від поточного часу. */
static void reschedule(uint32_t *next, uint32_t period, uint32_t now)
{
  *next += period;
  if (due(now, *next)) {
    *next = now + period;
  }
}

static bool cli_task(app_t *a)
{
  const app_hw_t *hw = a->hw;
  bool worked = false;
  int c;
  while ((c = hw->con_getc()) >= 0) {
    uint32_t t_eol = 0;
    bool stamped = c == '\n' && hw->rx_eol_take(&t_eol);
    worked = true;
    if (cli_feed(&a->cli, (char)c)) {
      cli_cmd_t cmd = a->cli.overflow ? (cli_cmd_t){ CLI_ERROR, 0, 0, 0 } : cli_parse(a->cli.buf);
      app_execute(a, &cmd);
      if (stamped && cmd.id == CLI_STATUS) {
        perf_add(&a->st[APP_ST_CMD_LAT], hw->cycles() - t_eol);
      }
    }
  }
  return worked;
}

static bool button_task(app_t *a)
{
  const app_hw_t *hw = a->hw;
  uint32_t t_event;
  if (a->btn_state == APP_BTN_WAIT_RELEASE) {
    uint32_t now = hw->time->millis();
    if (hw->button_pressed()) {
      a->btn_release_ms = now;
    } else if (now - a->btn_release_ms >= APP_BTN_RELEASE_MS) {
      (void)hw->btn_take(&t_event); /* спади брязкоту під час відпускання */
      a->btn_state = APP_BTN_IDLE;
    }
    return false;
  }
  if (!hw->btn_take(&t_event)) {
    return false;
  }
  hw->probe(APP_PROBE_REACT, true);
  perf_add(&a->st[APP_ST_BTN_LAT], hw->cycles() - t_event);
  a->btn_events++;
  app_evt(a, APP_EV_BTN_PRESS);
  app_next_mode(a);
  hw->probe(APP_PROBE_REACT, false);
  a->btn_state = APP_BTN_WAIT_RELEASE;
  a->btn_release_ms = hw->time->millis();
  return true;
}

static void imu_failed(app_t *a)
{
  const app_hw_t *hw = a->hw;
  uint32_t now = hw->time->millis();
  a->imu_err++;
  if (!a->imu_failing) {
    a->imu_failing = 1;
    a->imu_fail_ms = now;
  } else if (now - a->imu_fail_ms > APP_IMU_LOST_MS) {
    if (!a->imu_lost) {
      a->imu_lost = 1;
      app_evt(a, APP_EV_IMU_LOST);
      app_print(hw, "ERR,IMU lost,t_ms=%lu\r\n", (unsigned long)now);
    }
    a->imu_fail_ms = now - APP_IMU_LOST_MS + IMU_RECOVER_MS; /* наступне скидання через 100 мс */
    hw->i2c_recover(); /* завислий контролер сам не відновиться (затиснутий SDA, сценарій 6.2.4) */
  }
}

static void imu_ok(app_t *a)
{
  const app_hw_t *hw = a->hw;
  a->imu_failing = 0;
  if (a->imu_lost) {
    a->imu_lost = 0;
    (void)mpu6050_init(hw->mpu); /* сенсор міг втратити живлення: регістри скинуті */
    motion_reset(&a->motion);
    app_evt(a, APP_EV_IMU_RESTORED);
    app_print(hw, "IMU,restored,t_ms=%lu\r\n", (unsigned long)hw->time->millis());
  }
  if (!a->boot_reported) {
    a->boot_reported = 1;
    app_print(hw, "BOOT,first_imu_us=%lu\r\n", (unsigned long)hw->micros());
  }
}

static void cal_step(app_t *a, const mpu6050_data_t *v)
{
  for (int i = 0; i < 3; i++) {
    a->cal_sum[i] += v->gyro_mdps[i];
  }
  if (--a->cal_left == 0u) {
    for (int i = 0; i < 3; i++) {
      a->motion.gyro_bias[i] = a->cal_sum[i] / (int32_t)APP_CAL_SAMPLES;
    }
    orient_t acc;
    orient_from_accel(v->accel_mg, &acc);
    a->motion.zero = acc;
    a->motion.started = 0;
    app_print(a->hw, "CAL,done,pitch0_cdeg=%ld,roll0_cdeg=%ld,gyro_bias_mdps=%ld/%ld/%ld\r\n",
              (long)acc.pitch_cdeg, (long)acc.roll_cdeg, (long)a->motion.gyro_bias[0],
              (long)a->motion.gyro_bias[1], (long)a->motion.gyro_bias[2]);
  }
}

static bool imu_task(app_t *a)
{
  const app_hw_t *hw = a->hw;
  uint32_t now = hw->cycles();
  if (!due(now, a->imu_next)) {
    return false;
  }
  hw->probe(APP_PROBE_IMU, true);
  uint32_t dt_us = (now - a->imu_prev) / a->cyc_per_us;
  if (a->imu_prev_valid) {
    perf_jitter_add(&a->jit_imu, dt_us);
  }
  a->imu_prev = now;
  a->imu_prev_valid = 1;
  reschedule(&a->imu_next, a->imu_period_cyc, now);

  mpu6050_raw_t raw;
  uint32_t t0 = hw->cycles();
  dev_status_t st = mpu6050_read_raw(hw->mpu, &raw);
  perf_add(&a->st[APP_ST_I2C], hw->cycles() - t0);
  if (st != DEV_OK) {
    imu_failed(a);
  } else {
    imu_ok(a);
    mpu6050_data_t v;
    mpu6050_convert(&raw, &v);
    for (int i = 0; i < 3; i++) {
      int32_t d = v.accel_mg[i] - a->accel_prev[i];
      if (a->motion.started && (d > MOTION_STEP_MG || d < -MOTION_STEP_MG) && !a->motion_pending) {
        a->motion_pending = 1;
        a->t_motion = now;
        hw->mark(true);
      }
      a->accel_prev[i] = v.accel_mg[i];
    }
    motion_fuse(&a->motion, v.accel_mg, v.gyro_mdps, dt_us);
    if (a->cal_left != 0u) {
      cal_step(a, &v);
    }
  }
  hw->probe(APP_PROBE_IMU, false);
  return true;
}

static bool servo_task(app_t *a)
{
  const app_hw_t *hw = a->hw;
  uint32_t now = hw->cycles();
  if (!due(now, a->servo_next)) {
    return false;
  }
  if (a->servo_prev_valid) {
    perf_jitter_add(&a->jit_servo, (now - a->servo_prev) / a->cyc_per_us);
  }
  a->servo_prev = now;
  a->servo_prev_valid = 1;
  reschedule(&a->servo_next, a->servo_period_cyc, now);

  if ((a->mode != APP_MODE_LIVE && a->mode != APP_MODE_REC) || a->imu_lost) {
    return true; /* HOLD, PLAY або FR-10: серво лишаються в останньому положенні */
  }
  /* FR-05: за один період 20 мс не більше 300 °/с × 20 мс = 6° */
  int32_t max_step = (int32_t)(((uint64_t)MOTION_MAX_RATE_CDPS * APP_SERVO_PERIOD_US) / 1000000u);
  int32_t pan = motion_slew(hw->servo_pan->angle_cdeg, motion_to_servo_cdeg(a->motion.est.roll_cdeg),
                            MOTION_DEADZONE_CDEG, max_step);
  int32_t tilt = motion_slew(hw->servo_tilt->angle_cdeg, motion_to_servo_cdeg(a->motion.est.pitch_cdeg),
                             MOTION_DEADZONE_CDEG, max_step);
  bool changed = pan != hw->servo_pan->angle_cdeg || tilt != hw->servo_tilt->angle_cdeg;
  hw->probe(APP_PROBE_PWM, true);
  servo_set_cdeg(hw->servo_pan, pan);
  servo_set_cdeg(hw->servo_tilt, tilt);
  hw->probe(APP_PROBE_PWM, false);
  if (changed && a->motion_pending) {
    perf_add(&a->st[APP_ST_E2E], hw->cycles() - a->t_motion);
    a->motion_pending = 0;
    hw->mark(false);
  }
  return true;
}

static bool rec_task(app_t *a)
{
  if (a->mode != APP_MODE_REC) {
    return false;
  }
  uint32_t now = a->hw->cycles();
  if (!due(now, a->rec_next)) {
    return false;
  }
  reschedule(&a->rec_next, APP_REC_PERIOD_US * a->cyc_per_us, now);
  app_rec_sample(a);
  return true;
}

static bool play_task(app_t *a)
{
  if (a->mode != APP_MODE_PLAY) {
    return false;
  }
  uint32_t now = a->hw->cycles();
  if (!due(now, a->rec_next)) {
    return false;
  }
  reschedule(&a->rec_next, APP_REC_PERIOD_US * a->cyc_per_us, now);
  if (!app_play_sample(a)) {
    app_set_mode(a, APP_MODE_HOLD);
  }
  return true;
}

static void load_task(app_t *a)
{
  uint32_t window = a->hw->cycles() - a->window_start;
  if (window < a->hw->sysclk_hz) {
    return;
  }
  uint8_t pct = (uint8_t)(((uint64_t)a->busy_cycles * 100u) / window);
  a->load_last = pct;
  if (a->load_min > pct) {
    a->load_min = pct;
  }
  if (a->load_max < pct) {
    a->load_max = pct;
  }
  a->busy_cycles = 0;
  a->window_start = a->hw->cycles();
}

/** Час рахується лише для проходів, де задача справді працювала (методичка §5.1, приклад 4). */
static void run_task(app_t *a, app_stat_t id, bool (*task)(app_t *))
{
  uint32_t t0 = a->hw->cycles();
  if (task(a)) {
    uint32_t d = a->hw->cycles() - t0;
    perf_add(&a->st[id], d);
    a->busy_cycles += d;
  }
}

/* ---- Журнал подій і скидання статистики ---------------------------------- */
void app_evt(app_t *a, app_event_t id)
{
  app_evt_t *e = &a->evt[a->evt_n & (APP_EVT_LOG - 1u)];
  e->t_us = a->hw->micros();
  e->id = id;
  a->evt_n++;
}

void app_stats_reset(app_t *a)
{
  for (unsigned i = 0; i < APP_ST_COUNT; i++) {
    a->st[i] = (perf_stat_t){ 0, 0, 0, 0 };
  }
  perf_jitter_reset(&a->jit_imu, a->imu_period_cyc / a->cyc_per_us, APP_IMU_TOL_US);
  perf_jitter_reset(&a->jit_servo, APP_SERVO_PERIOD_US, APP_IMU_TOL_US);
  a->imu_prev_valid = 0;
  a->servo_prev_valid = 0;
  a->busy_cycles = 0;
  a->window_start = a->hw->cycles();
  a->load_last = 0;
  a->load_min = 100;
  a->load_max = 0;
}

/* ---- Старт і головний цикл ----------------------------------------------- */
void app_init(app_t *app, const app_hw_t *hw)
{
  app->hw = hw;
  app->cyc_per_us = hw->sysclk_hz / 1000000u;
  app->mode = APP_MODE_HOLD;
  app->btn_state = APP_BTN_IDLE;
  app->btn_events = 0;
  app->imu_lost = 0;
  app->imu_failing = 0;
  app->boot_reported = 0;
  app->imu_err = 0;
  app->cal_left = 0;
  app->motion_pending = 0;
  app->tear_next_page = 0;
  app->evt_n = 0;
  motion_reset(&app->motion);
  app->cli = (cli_reader_t){ { 0 }, 0, 0, 0 };

  app_print(hw, "\r\nBOOT,fw=%s,git=%s,board=%s\r\n", hw->fw_version, hw->fw_git, hw->board_name);
  app_evt(app, APP_EV_BOOT);
  selftest_result_t r = selftest_run(hw);
  if (hw->mpu->who_am_i != MPU6050_WHO_AM_I_VALUE) {
    app->imu_lost = 1; /* сенсор не готовий: imu_task повторює читання, FR-10 відновить роботу */
    app->imu_failing = 1;
    app->imu_fail_ms = hw->time->millis();
  }
  (void)r;
  servo_set_limits(hw->servo_pan, 10, 170); /* FR-04: межі за замовчуванням */
  servo_set_limits(hw->servo_tilt, 10, 170);
  servo_set_deg(hw->servo_pan, 90);
  servo_set_deg(hw->servo_tilt, 90);

  app_traj_info_t info;
  app_traj_scan(app, &info);
  app_print(hw, "TRAJ,session=%u,pages=%lu,samples=%lu,torn=%u\r\n", info.session, (unsigned long)info.pages,
            (unsigned long)info.samples, info.torn);

  app->imu_period_cyc = APP_IMU_PERIOD_US * app->cyc_per_us;
  app->servo_period_cyc = APP_SERVO_PERIOD_US * app->cyc_per_us;
  app_stats_reset(app);
  app_set_mode(app, APP_MODE_LIVE);
  app_print(hw, "READY\r\n");
  uint32_t now = hw->cycles();
  app->imu_next = now;
  app->servo_next = now;
  app->rec_next = now;
}

void app_poll(app_t *a)
{
  uint32_t t_loop = a->hw->cycles();
  run_task(a, APP_ST_CLI, cli_task);
  run_task(a, APP_ST_BTN, button_task);
  run_task(a, APP_ST_IMU, imu_task);
  run_task(a, APP_ST_SERVO, servo_task);
  run_task(a, APP_ST_REC, rec_task);
  run_task(a, APP_ST_PLAY, play_task);
  load_task(a);
  perf_add(&a->st[APP_ST_LOOP], a->hw->cycles() - t_loop);
}
