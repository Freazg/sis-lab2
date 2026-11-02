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
  a->hw->mode_post((int)next[a->mode]);
}

/* ---- Тіла задач ----------------------------------------------------------- */
void app_cli_step(app_t *a)
{
  const app_hw_t *hw = a->hw;
  int c;
  while ((c = hw->con_getc()) >= 0) {
    uint32_t t_eol = 0;
    bool stamped = c == '\n' && hw->rx_eol_take(&t_eol);
    if (cli_feed(&a->cli, (char)c)) {
      uint32_t t0 = hw->cycles();
      cli_cmd_t cmd = a->cli.overflow ? (cli_cmd_t){ CLI_ERROR, 0, 0, 0 } : cli_parse(a->cli.buf);
      app_execute(a, &cmd);
      uint32_t t1 = hw->cycles();
      perf_add(&a->st[APP_ST_CLI], t1 - t0);
      if (stamped && cmd.id == CLI_STATUS) {
        perf_add(&a->st[APP_ST_CMD_LAT], t1 - t_eol);
      }
    }
  }
}

void app_btn_event(app_t *a, uint32_t t_event)
{
  const app_hw_t *hw = a->hw;
  uint32_t t0 = hw->cycles();
  hw->probe(APP_PROBE_REACT, true);
  perf_add(&a->st[APP_ST_BTN_LAT], t0 - t_event);
  a->btn_events++;
  app_evt(a, APP_EV_BTN_PRESS);
  app_next_mode(a); /* сам перехід (друк, флеш) — у задачі traj */
  hw->probe(APP_PROBE_REACT, false);
  perf_add(&a->st[APP_ST_BTN], hw->cycles() - t0);
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
      app_printq(hw, "ERR,IMU lost,t_ms=%lu\r\n", (unsigned long)now);
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
    app_printq(hw, "IMU,restored,t_ms=%lu\r\n", (unsigned long)hw->time->millis());
  }
  if (!a->boot_reported) {
    a->boot_reported = 1;
    app_printq(hw, "BOOT,first_imu_us=%lu\r\n", (unsigned long)hw->micros());
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
    app_printq(a->hw, "CAL,done,pitch0_cdeg=%ld,roll0_cdeg=%ld,gyro_bias_mdps=%ld/%ld/%ld\r\n",
               (long)acc.pitch_cdeg, (long)acc.roll_cdeg, (long)a->motion.gyro_bias[0],
               (long)a->motion.gyro_bias[1], (long)a->motion.gyro_bias[2]);
  }
}

void app_imu_step(app_t *a, uint32_t now)
{
  const app_hw_t *hw = a->hw;
  hw->probe(APP_PROBE_IMU, true);
  uint32_t dt_us = (now - a->imu_prev) / a->cyc_per_us;
  if (a->imu_prev_valid) {
    perf_jitter_add(&a->jit_imu, dt_us);
  }
  a->imu_prev = now;
  a->imu_prev_valid = 1;

  if (!hw->lock(APP_LOCK_I2C, APP_I2C_WAIT_MS)) {
    a->i2c_busy++; /* шину тримає selftest або cap: семпл пропущено, період не зсувається */
    hw->probe(APP_PROBE_IMU, false);
    perf_add(&a->st[APP_ST_IMU], hw->cycles() - now);
    return;
  }
  mpu6050_raw_t raw;
  uint32_t t0 = hw->cycles();
  dev_status_t st = mpu6050_read_raw(hw->mpu, &raw);
  perf_add(&a->st[APP_ST_I2C], hw->cycles() - t0);
  if (st != DEV_OK) {
    imu_failed(a);
    hw->unlock(APP_LOCK_I2C);
  } else {
    imu_ok(a);
    hw->unlock(APP_LOCK_I2C);
    mpu6050_data_t v;
    mpu6050_convert(&raw, &v);
    for (int i = 0; i < 3; i++) {
      int32_t d = v.accel_mg[i] - a->accel_prev[i];
      if (a->motion.started && (d > MOTION_STEP_MG || d < -MOTION_STEP_MG) && !a->motion_pending) {
        a->t_motion = now;
        a->motion_pending = 1;
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
  perf_add(&a->st[APP_ST_IMU], hw->cycles() - now);
}

static void servo_follow(app_t *a)
{
  const app_hw_t *hw = a->hw;
  if (a->imu_lost) {
    return; /* FR-10: серво лишаються в останньому положенні */
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
}

void app_servo_step(app_t *a, uint32_t now)
{
  const app_hw_t *hw = a->hw;
  if (a->servo_prev_valid) {
    perf_jitter_add(&a->jit_servo, (now - a->servo_prev) / a->cyc_per_us);
  }
  a->servo_prev = now;
  a->servo_prev_valid = 1;

  app_mode_t m = a->mode;
  if (m == APP_MODE_LIVE || m == APP_MODE_REC) {
    servo_follow(a);
  }
  perf_add(&a->st[APP_ST_SERVO], hw->cycles() - now);
  if (m == APP_MODE_REC) {
    /* FR-07: семпл 50 Гц — кут, щойно виставлений на серво; у флеш його пише задача traj */
    hw->rec_post((int16_t)(hw->servo_pan->angle_cdeg / 10), (int16_t)(hw->servo_tilt->angle_cdeg / 10));
  } else if (m == APP_MODE_PLAY) {
    uint32_t t0 = hw->cycles();
    if (!a->play_done && !app_play_sample(a)) {
      a->play_done = 1; /* до виходу з PLAY задача traj може не встигнути: запит лише один */
      hw->mode_post(APP_MODE_HOLD);
    }
    perf_add(&a->st[APP_ST_PLAY], hw->cycles() - t0);
  }
}

void app_rec_add(app_t *a, int16_t pan, int16_t tilt)
{
  if (a->mode != APP_MODE_REC) {
    return; /* семпли, що лишились у черзі після виходу з REC */
  }
  uint32_t t0 = a->hw->cycles();
  app_rec_sample(a, (traj_sample_t){ pan, tilt });
  perf_add(&a->st[APP_ST_REC], a->hw->cycles() - t0);
}

void app_long_run(app_t *a, app_long_op_t op)
{
  if (op == APP_LONG_DUMP) {
    app_traj_dump(a);
  } else {
    app_traj_erase(a);
  }
  a->long_busy = 0;
}

void app_load_update(app_t *a, uint8_t pct)
{
  a->load_last = pct;
  if (a->load_min > pct) {
    a->load_min = pct;
  }
  if (a->load_max < pct) {
    a->load_max = pct;
  }
}

/* ---- Журнал подій і скидання статистики ---------------------------------- */
void app_evt(app_t *a, app_event_t id)
{
  uint32_t t = a->hw->micros();
  a->hw->crit(true); /* журнал пишуть кілька задач */
  app_evt_t *e = &a->evt[a->evt_n & (APP_EVT_LOG - 1u)];
  e->t_us = t;
  e->id = id;
  a->evt_n++;
  a->hw->crit(false);
}

void app_stats_reset(app_t *a)
{
  a->hw->crit(true);
  for (unsigned i = 0; i < APP_ST_COUNT; i++) {
    a->st[i] = (perf_stat_t){ 0, 0, 0, 0 };
  }
  perf_jitter_reset(&a->jit_imu, a->imu_period_us, APP_IMU_TOL_US);
  perf_jitter_reset(&a->jit_servo, APP_SERVO_PERIOD_US, APP_IMU_TOL_US);
  a->imu_prev_valid = 0;
  a->servo_prev_valid = 0;
  a->load_last = 0;
  a->load_min = 100;
  a->load_max = 0;
  a->hw->crit(false);
}

/* ---- Старт ----------------------------------------------------------------- */
void app_init(app_t *app, const app_hw_t *hw)
{
  app->hw = hw;
  app->cyc_per_us = hw->sysclk_hz / 1000000u;
  app->mode = APP_MODE_HOLD;
  app->btn_events = 0;
  app->imu_lost = 0;
  app->imu_failing = 0;
  app->boot_reported = 0;
  app->imu_err = 0;
  app->i2c_busy = 0;
  app->cal_left = 0;
  app->motion_pending = 0;
  app->tear_next_page = 0;
  app->long_busy = 0;
  app->evt_n = 0;
  motion_reset(&app->motion);
  app->cli = (cli_reader_t){ { 0 }, 0, 0, 0 };

  app_print(hw, "\r\nBOOT,fw=%s,git=%s,board=%s\r\n", hw->fw_version, hw->fw_git, hw->board_name);
  app_evt(app, APP_EV_BOOT);
  selftest_result_t r = selftest_run(hw);
  if (hw->mpu->who_am_i != MPU6050_WHO_AM_I_VALUE) {
    app->imu_lost = 1; /* сенсор не готовий: задача imu повторює читання, FR-10 відновить роботу */
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

  app->imu_period_us = APP_IMU_PERIOD_US;
  app_stats_reset(app);
  app_set_mode(app, APP_MODE_LIVE);
  app_print(hw, "READY\r\n");
}
