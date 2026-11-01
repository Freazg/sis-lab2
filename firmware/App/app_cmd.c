#include "app_internal.h"
#include "app_io.h"
#include "selftest.h"

static const char *const stat_names[APP_ST_COUNT] = {
  "cli", "btn", "imu", "servo", "rec", "play", "loop",
  "i2c_read", "flash_prog", "flash_erase", "flash_read",
  "btn_latency", "cmd_latency", "e2e_latency",
};

static const char *const evt_names[APP_EV_COUNT] = {
  "boot", "btn_press", "mode", "erase_start", "erase_done", "prog_start", "prog_done",
  "dump_start", "dump_done", "imu_lost", "imu_restored",
};

static void print_help(const app_hw_t *hw)
{
  app_print(hw, "cmds: help selftest status cal dump erase mode live|hold|rec|play limits <a> <b>\r\n");
  app_print(hw, "      servo pan|tilt <0..180> cap i2c|spi|pwm|uart|anomaly\r\n");
  app_print(hw, "      stats [reset] hist mem evt perfcal rate <hz> reset\r\n");
  app_print(hw, "      fault powerloss\r\n");
}

static void print_status(app_t *a)
{
  const app_hw_t *hw = a->hw;
  app_print(hw, "STATUS,mode=%s,t_ms=%lu,pitch_cdeg=%ld,roll_cdeg=%ld,pan_cdeg=%ld,tilt_cdeg=%ld,imu=%s,rec=%lu\r\n",
            app_mode_name(a->mode), (unsigned long)hw->time->millis(), (long)a->motion.est.pitch_cdeg,
            (long)a->motion.est.roll_cdeg, (long)hw->servo_pan->angle_cdeg, (long)hw->servo_tilt->angle_cdeg,
            a->imu_lost ? "LOST" : "OK", (unsigned long)a->traj_samples);
}

static void print_jitter(const app_hw_t *hw, const char *name, const perf_jitter_t *j)
{
  app_print(hw, "JITTER name=%s period_us=%lu n=%lu min_us=%ld max_us=%ld p99_us=%lu missed=%lu viol=%lu\r\n",
            name, (unsigned long)j->period_us, (unsigned long)j->n, (long)j->min_us, (long)j->max_us,
            (unsigned long)perf_jitter_percentile_us(j, 99), (unsigned long)j->missed, (unsigned long)j->viol);
}

static void print_hist(const app_t *a)
{
  const app_hw_t *hw = a->hw;
  app_print(hw, "HIST name=imu bin_us=%u", PERF_HIST_BIN_US);
  for (unsigned i = 0; i < PERF_HIST_BINS; i++) {
    app_print(hw, " %lu", (unsigned long)a->jit_imu.hist[i]);
  }
  app_print(hw, "\r\n");
}

void app_print_stats(app_t *a)
{
  const app_hw_t *hw = a->hw;
  uint32_t k = a->cyc_per_us;
  app_print(hw, "STATS,BEGIN\r\n");
  for (unsigned i = 0; i < APP_ST_COUNT; i++) {
    const perf_stat_t *s = &a->st[i];
    app_print(hw, "STAT name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu\r\n", stat_names[i],
              (unsigned long)s->count, (unsigned long)(s->min / k), (unsigned long)(perf_avg(s) / k),
              (unsigned long)(s->max / k));
  }
  print_jitter(hw, "imu", &a->jit_imu);
  print_jitter(hw, "servo", &a->jit_servo);
  app_print(hw, "LOAD pct=%u min=%u max=%u\r\n", a->load_last, a->load_min, a->load_max);
  app_print(hw, "CNT rx_dropped=%lu imu_err=%lu btn=%lu\r\n", (unsigned long)hw->rx_dropped(),
            (unsigned long)a->imu_err, (unsigned long)a->btn_events);
  print_hist(a); /* разом зі stats: вивід окремої команди сам додав би джитер */
  app_print(hw, "STATS,END\r\n");
}

static void print_mem(const app_hw_t *hw)
{
  app_print(hw, "STACK used=%lu reserved=%lu\r\n", (unsigned long)hw->stack_used(), (unsigned long)hw->stack_size());
}

void app_print_evt(app_t *a)
{
  const app_hw_t *hw = a->hw;
  uint32_t first = a->evt_n > APP_EVT_LOG ? a->evt_n - APP_EVT_LOG : 0u;
  for (uint32_t i = first; i < a->evt_n; i++) {
    const app_evt_t *e = &a->evt[i & (APP_EVT_LOG - 1u)];
    app_print(hw, "EVT t_us=%lu name=%s\r\n", (unsigned long)e->t_us, evt_names[e->id]);
  }
}

static void perfcal(const app_hw_t *hw)
{
  enum { N = 1000 };
  uint32_t backward = 0;
  uint32_t prev = hw->cycles();
  uint32_t t0 = hw->cycles();
  for (int i = 0; i < N; i++) {
    uint32_t t = hw->cycles();
    backward += (int32_t)(t - prev) < 0;
    prev = t;
  }
  uint32_t read_cyc = (hw->cycles() - t0) / N;
  t0 = hw->cycles();
  for (int i = 0; i < N; i++) {
    hw->probe(APP_PROBE_REACT, true);
    hw->probe(APP_PROBE_REACT, false);
  }
  uint32_t probe_cyc = (hw->cycles() - t0) / N;
  t0 = hw->cycles();
  app_print(hw, "PERFCAL,line_of_40_bytes_xxxxxxxxxxxx\r\n");
  uint32_t line_us = (hw->cycles() - t0) / (hw->sysclk_hz / 1000000u);
  app_print(hw, "PERFCAL loop500_cyc=%lu\r\n", (unsigned long)hw->cal_loop());
  app_print(hw, "PERFCAL read_cyc=%lu probe_pair_cyc=%lu line40_us=%lu backward=%lu\r\n", (unsigned long)read_cyc,
            (unsigned long)probe_cyc, (unsigned long)line_us, (unsigned long)backward);
}

static void set_limits(app_t *a, int32_t lo, int32_t hi)
{
  const app_hw_t *hw = a->hw;
  if (lo < 0 || hi > 180 || lo >= hi) {
    app_print(hw, "ERR,limits\r\n");
    return;
  }
  servo_set_limits(hw->servo_pan, (uint32_t)lo, (uint32_t)hi);
  servo_set_limits(hw->servo_tilt, (uint32_t)lo, (uint32_t)hi);
  app_print(hw, "LIMITS,min=%ld,max=%ld\r\n", (long)lo, (long)hi);
}

static void set_rate(app_t *a, int32_t hz)
{
  const app_hw_t *hw = a->hw;
  if (hz < 10 || hz > 2000) {
    app_print(hw, "ERR,rate\r\n");
    return;
  }
  a->imu_period_cyc = (hw->sysclk_hz / (uint32_t)hz);
  app_stats_reset(a);
  app_print(hw, "RATE,hz=%ld,period_us=%lu\r\n", (long)hz, (unsigned long)(a->imu_period_cyc / a->cyc_per_us));
}

void app_execute(app_t *a, const cli_cmd_t *cmd)
{
  const app_hw_t *hw = a->hw;

  switch (cmd->id) {
    case CLI_NONE:
      return;
    case CLI_HELP:
      print_help(hw);
      break;
    case CLI_SELFTEST:
      selftest_run(hw);
      break;
    case CLI_STATUS:
      print_status(a);
      break;
    case CLI_CAL:
      a->cal_left = APP_CAL_SAMPLES;
      a->cal_sum[0] = a->cal_sum[1] = a->cal_sum[2] = 0;
      app_print(hw, "CAL,start\r\n");
      break;
    case CLI_MODE_LIVE:
      app_set_mode(a, APP_MODE_LIVE);
      break;
    case CLI_MODE_HOLD:
      app_set_mode(a, APP_MODE_HOLD);
      break;
    case CLI_MODE_REC:
      app_set_mode(a, APP_MODE_REC);
      break;
    case CLI_MODE_PLAY:
      app_set_mode(a, APP_MODE_PLAY);
      break;
    case CLI_LIMITS:
      set_limits(a, cmd->value, cmd->value2);
      break;
    case CLI_DUMP:
    case CLI_ERASE:
      if (a->mode == APP_MODE_REC || a->mode == APP_MODE_PLAY) {
        app_print(hw, "ERR,busy\r\n"); /* один буфер сторінки на запис, відтворення і dump */
      } else if (cmd->id == CLI_DUMP) {
        app_traj_dump(a);
      } else {
        app_traj_erase(a);
      }
      break;
    case CLI_SERVO: {
      servo_t *s = cmd->servo_index == 0 ? hw->servo_pan : hw->servo_tilt;
      uint32_t us = servo_set_deg(s, cmd->value);
      app_print(hw, "SERVO,%s,deg=%lu,pulse_us=%lu\r\n", cmd->servo_index == 0 ? "pan" : "tilt",
                (unsigned long)s->angle_deg, (unsigned long)us);
      break;
    }
    case CLI_CAP_I2C:
    case CLI_CAP_SPI:
    case CLI_CAP_PWM:
    case CLI_CAP_UART:
    case CLI_CAP_ANOMALY:
      app_cap(a, cmd->id);
      break;
    case CLI_STATS:
      app_print_stats(a);
      break;
    case CLI_STATS_RESET:
      app_stats_reset(a);
      app_print(hw, "STATS,RESET\r\n");
      break;
    case CLI_HIST:
      print_hist(a);
      break;
    case CLI_MEM:
      print_mem(hw);
      break;
    case CLI_EVT:
      app_print_evt(a);
      break;
    case CLI_PERFCAL:
      perfcal(hw);
      break;
    case CLI_RATE:
      set_rate(a, cmd->value);
      break;
    case CLI_RESET:
      hw->reset();
      break;
    case CLI_FAULT_POWERLOSS:
      a->tear_next_page = 1;
      app_print(hw, "FAULT,powerloss_armed\r\n");
      break;
    case CLI_ERROR:
    default:
      app_print(hw, "ERR,unknown_command\r\n");
      break;
  }
}
