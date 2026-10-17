#include "app.h"

#include "app_io.h"
#include "selftest.h"

#define CAP_FLASH_ADDR 0x7FF100u /* сторінка в тестовому секторі ST_FLASH_TEST_ADDR */
#define CAP_FLASH_LEN  16u
#define CAP_PWM_HOLD_MS 100u     /* 5 періодів ШІМ на кожне значення */

static void set_mode(app_t *app, app_mode_t m)
{
  app->mode = m;
  app->hw->led_set(APP_LED_LIVE, m == APP_MODE_LIVE);
  app->hw->led_set(APP_LED_REC, m == APP_MODE_HOLD);
  app_print(app->hw, "MODE,%s\r\n", m == APP_MODE_LIVE ? "LIVE" : "HOLD");
}

static void print_help(const app_hw_t *hw)
{
  app_print(hw, "cmds: help | selftest | status | mode live|hold | servo pan|tilt <0..180>\r\n");
  app_print(hw, "      cap i2c|spi|pwm|uart|anomaly\r\n");
}

static void print_status(app_t *app)
{
  const app_hw_t *hw = app->hw;
  app_print(hw, "STATUS,mode=%s,t_ms=%lu,pitch_cdeg=%ld,roll_cdeg=%ld,pan_deg=%ld,tilt_deg=%ld\r\n",
            app->mode == APP_MODE_LIVE ? "LIVE" : "HOLD", (unsigned long)hw->time->millis(),
            (long)app->orient.pitch_cdeg, (long)app->orient.roll_cdeg, (long)app->pan_deg,
            (long)app->tilt_deg);
}

/* ---- Відтворювані транзакції для логічного аналізатора (docs/captures) ---- */

static void cap_i2c(const app_hw_t *hw)
{
  uint8_t who = 0;
  uint8_t raw_bytes[MPU6050_BURST_LEN] = { 0 };
  const uint8_t reg_who = MPU6050_REG_WHO_AM_I;
  const uint8_t reg_acc = MPU6050_REG_ACCEL_XOUT_H;

  hw->mark(true); /* фронт MARK: подія в прошивці перед зверненням до шини */
  dev_status_t st1 = hw->i2c->write_read(hw->i2c->ctx, MPU6050_ADDR_AD0_LOW, &reg_who, 1, &who, 1);
  hw->mark(false);
  hw->time->delay_ms(1);
  hw->mark(true);
  dev_status_t st2 = hw->i2c->write_read(hw->i2c->ctx, MPU6050_ADDR_AD0_LOW, &reg_acc, 1, raw_bytes,
                                         sizeof raw_bytes);
  hw->mark(false);

  mpu6050_raw_t raw;
  mpu6050_data_t v;
  mpu6050_parse(raw_bytes, &raw);
  mpu6050_convert(&raw, &v);
  app_print(hw, "CAP,i2c,who_am_i=0x%02X,st=%s\r\n", who, dev_status_str(st1));
  app_print(hw, "CAP,i2c,burst=");
  for (unsigned i = 0; i < sizeof raw_bytes; i++) {
    app_print(hw, "%02X", raw_bytes[i]);
  }
  app_print(hw, ",st=%s\r\n", dev_status_str(st2));
  app_print(hw, "CAP,i2c,acc_mg=%ld/%ld/%ld,temp_cdeg=%ld,gyro_mdps=%ld/%ld/%ld\r\n", (long)v.accel_mg[0],
            (long)v.accel_mg[1], (long)v.accel_mg[2], (long)v.temp_cdeg, (long)v.gyro_mdps[0],
            (long)v.gyro_mdps[1], (long)v.gyro_mdps[2]);
}

static void cap_spi(const app_hw_t *hw)
{
  uint8_t id[3] = { 0 };
  uint8_t wr[CAP_FLASH_LEN];
  uint8_t rd[CAP_FLASH_LEN] = { 0 };
  selftest_flash_pattern(wr, sizeof wr, 0xA5);

  hw->mark(true);
  dev_status_t st = w25q64_read_jedec(hw->flash, id);
  hw->mark(false);
  app_print(hw, "CAP,spi,jedec=%02X%02X%02X,st=%s\r\n", id[0], id[1], id[2], dev_status_str(st));

  hw->mark(true);
  st = w25q64_page_program(hw->flash, CAP_FLASH_ADDR, wr, sizeof wr); /* WREN -> 02h -> poll BUSY */
  hw->mark(false);
  app_print(hw, "CAP,spi,program_addr=0x%06lX,len=%u,st=%s\r\n", (unsigned long)CAP_FLASH_ADDR,
            (unsigned)sizeof wr, dev_status_str(st));

  hw->mark(true);
  st = w25q64_read(hw->flash, CAP_FLASH_ADDR, rd, sizeof rd);
  hw->mark(false);
  app_print(hw, "CAP,spi,read=");
  for (unsigned i = 0; i < sizeof rd; i++) {
    app_print(hw, "%02X", rd[i]);
  }
  app_print(hw, ",st=%s\r\n", dev_status_str(st));
}

static void cap_pwm(const app_hw_t *hw)
{
  static const int32_t seq[][2] = { { 0, 45 }, { 90, 90 }, { 180, 135 } };
  for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
    hw->mark(true);
    uint32_t p = servo_set_deg(hw->servo_pan, seq[i][0]);
    uint32_t t = servo_set_deg(hw->servo_tilt, seq[i][1]);
    hw->mark(false);
    app_print(hw, "CAP,pwm,pan_deg=%ld,pan_us=%lu,tilt_deg=%ld,tilt_us=%lu\r\n", (long)seq[i][0],
              (unsigned long)p, (long)seq[i][1], (unsigned long)t);
    hw->time->delay_ms(CAP_PWM_HOLD_MS);
  }
}

static void cap_uart(const app_hw_t *hw)
{
  hw->mark(true);
  app_print(hw, "CAP,uart,payload=LR2-UART-0123456789\r\n");
  hw->mark(false);
  dev_status_t st = app_print_aux(hw, "CAP,uart,aux_payload=LR2-FT232RL-0123456789\r\n");
  app_print(hw, "CAP,uart,aux_st=%s\r\n", dev_status_str(st));
}

static void cap_anomaly(const app_hw_t *hw)
{
  uint8_t v = 0;
  const uint8_t reg = MPU6050_REG_WHO_AM_I;

  /* 1) неіснуюча адреса: AD0=1 -> 0x69, на стенді AD0=0 -> очікуємо NACK */
  hw->mark(true);
  dev_status_t st = hw->i2c->write_read(hw->i2c->ctx, MPU6050_ADDR_AD0_HIGH, &reg, 1, &v, 1);
  hw->mark(false);
  app_print(hw, "CAP,anomaly,step=wrong_addr,addr=0x69,st=%s\r\n", dev_status_str(st));
  hw->time->delay_ms(1);

  /* 2) HAL_I2C_Mem_Read (repeated START) — у симуляторі шина зависає після байта регістра */
  hw->mark(true);
  st = hw->i2c_mem_read_restart(MPU6050_ADDR_AD0_LOW, reg, &v);
  hw->mark(false);
  app_print(hw, "CAP,anomaly,step=mem_read_restart,val=0x%02X,st=%s\r\n", v, dev_status_str(st));

  /* 3) звичайне читання до відновлення — шина зайнята */
  v = 0;
  st = hw->i2c->write_read(hw->i2c->ctx, MPU6050_ADDR_AD0_LOW, &reg, 1, &v, 1);
  app_print(hw, "CAP,anomaly,step=read_before_recover,val=0x%02X,st=%s\r\n", v, dev_status_str(st));

  /* 4) відновлення периферії і повторне читання */
  hw->i2c_recover();
  hw->mark(true);
  v = 0;
  st = hw->i2c->write_read(hw->i2c->ctx, MPU6050_ADDR_AD0_LOW, &reg, 1, &v, 1);
  hw->mark(false);
  app_print(hw, "CAP,anomaly,step=read_after_recover,val=0x%02X,st=%s\r\n", v, dev_status_str(st));
}

void app_execute(app_t *app, const cli_cmd_t *cmd)
{
  const app_hw_t *hw = app->hw;
  static const char *const cap_names[] = { "i2c", "spi", "pwm", "uart", "anomaly" };

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
      print_status(app);
      break;
    case CLI_MODE_LIVE:
      set_mode(app, APP_MODE_LIVE);
      break;
    case CLI_MODE_HOLD:
      set_mode(app, APP_MODE_HOLD);
      break;
    case CLI_SERVO: {
      servo_t *s = cmd->servo_index == 0 ? hw->servo_pan : hw->servo_tilt;
      uint32_t us = servo_set_deg(s, cmd->value);
      if (cmd->servo_index == 0) {
        app->pan_deg = (int32_t)s->angle_deg;
      } else {
        app->tilt_deg = (int32_t)s->angle_deg;
      }
      app_print(hw, "SERVO,%s,deg=%lu,pulse_us=%lu\r\n", cmd->servo_index == 0 ? "pan" : "tilt",
                (unsigned long)s->angle_deg, (unsigned long)us);
      break;
    }
    case CLI_CAP_I2C:
    case CLI_CAP_SPI:
    case CLI_CAP_PWM:
    case CLI_CAP_UART:
    case CLI_CAP_ANOMALY: {
      unsigned k = (unsigned)(cmd->id - CLI_CAP_I2C);
      app_print(hw, "CAP,%s,BEGIN\r\n", cap_names[k]);
      switch (cmd->id) {
        case CLI_CAP_I2C: cap_i2c(hw); break;
        case CLI_CAP_SPI: cap_spi(hw); break;
        case CLI_CAP_PWM: cap_pwm(hw); break;
        case CLI_CAP_UART: cap_uart(hw); break;
        default: cap_anomaly(hw); break;
      }
      app_print(hw, "CAP,%s,END\r\n", cap_names[k]);
      break;
    }
    case CLI_ERROR:
    default:
      app_print(hw, "ERR,unknown_command\r\n");
      break;
  }
}

void app_on_button(app_t *app, uint8_t pressed)
{
  if (pressed && !app->btn_prev) {
    set_mode(app, app->mode == APP_MODE_LIVE ? APP_MODE_HOLD : APP_MODE_LIVE);
  }
  app->btn_prev = pressed;
}

void app_init(app_t *app, const app_hw_t *hw)
{
  app->hw = hw;
  app->mode = APP_MODE_LIVE;
  app->btn_prev = 0;
  app->pan_deg = 90;
  app->tilt_deg = 90;
  app->orient.pitch_cdeg = 0;
  app->orient.roll_cdeg = 0;
  app->cli = (cli_reader_t){ { 0 }, 0, 0, 0 };

  app_print(hw, "\r\n==== LR2 Motion-Mirroring: %s, fw %s ====\r\n", hw->board_name, hw->fw_version);
  selftest_result_t r = selftest_run(hw);
  app->mpu_ok = (uint8_t)(hw->mpu->who_am_i == MPU6050_WHO_AM_I_VALUE);
  (void)r;
  servo_set_deg(hw->servo_pan, app->pan_deg);
  servo_set_deg(hw->servo_tilt, app->tilt_deg);
  set_mode(app, APP_MODE_LIVE);
  print_help(hw);
  app_print(hw, "READY\r\n");
  app->next_ctrl_ms = hw->time->millis();
}

void app_poll(app_t *app)
{
  const app_hw_t *hw = app->hw;

  int c;
  while ((c = hw->con_getc()) >= 0) {
    if (cli_feed(&app->cli, (char)c)) {
      cli_cmd_t cmd = app->cli.overflow ? (cli_cmd_t){ CLI_ERROR, 0, 0 } : cli_parse(app->cli.buf);
      app_execute(app, &cmd);
    }
  }

  uint32_t now = hw->time->millis();
  if ((int32_t)(now - app->next_ctrl_ms) < 0) {
    return;
  }
  app->next_ctrl_ms = now + APP_CONTROL_PERIOD_MS;

  app_on_button(app, (uint8_t)hw->button_pressed());

  if (app->mode != APP_MODE_LIVE || !app->mpu_ok) {
    return;
  }
  mpu6050_raw_t raw;
  if (mpu6050_read_raw(hw->mpu, &raw) != DEV_OK) {
    return;
  }
  mpu6050_data_t v;
  mpu6050_convert(&raw, &v);
  orient_from_accel(v.accel_mg, &app->orient);
  app->pan_deg = orient_to_servo_deg(app->orient.roll_cdeg);
  app->tilt_deg = orient_to_servo_deg(app->orient.pitch_cdeg);
  servo_set_deg(hw->servo_pan, app->pan_deg);
  servo_set_deg(hw->servo_tilt, app->tilt_deg);
}
