#include "selftest.h"

#include <stdio.h>
#include <string.h>

#include "app_io.h"

static void verdict(const app_hw_t *hw, selftest_result_t *r, const char *dev, int ok, const char *details)
{
  app_print(hw, "ST,dev=%s,res=%s%s%s\r\n", dev, ok ? "PASS" : "FAIL", details[0] ? "," : "", details);
  if (ok) {
    r->pass++;
  } else {
    r->fail++;
  }
}

void selftest_flash_pattern(uint8_t *buf, uint32_t len, uint8_t seed)
{
  for (uint32_t i = 0; i < len; i++) {
    buf[i] = (uint8_t)(i * 37u + seed);
  }
}

/* ---- MCU ---------------------------------------------------------------- */
static void check_mcu(const app_hw_t *hw, selftest_result_t *r, char *d, size_t n)
{
  snprintf(d, n, "board=%s,sysclk_hz=%lu", hw->board_name, (unsigned long)hw->sysclk_hz);
  verdict(hw, r, "MCU", hw->sysclk_hz == 48000000u, d);
}

/* ---- MPU-6050 ----------------------------------------------------------- */
static void check_mpu(const app_hw_t *hw, selftest_result_t *r, char *d, size_t n)
{
  mpu6050_t *m = hw->mpu;
  dev_status_t st = mpu6050_probe(m, hw->i2c, hw->time, MPU6050_ADDR_AD0_LOW);
  if (st != DEV_OK) {
    snprintf(d, n, "bus=I2C1,addr=0x%02X,who=0x%02X,err=%s", MPU6050_ADDR_AD0_LOW, m->who_am_i,
             dev_status_str(st));
    verdict(hw, r, "MPU6050", 0, d);
    return;
  }
  st = mpu6050_init(m);
  mpu6050_raw_t raw;
  if (st == DEV_OK) {
    st = mpu6050_read_raw(m, &raw);
  }
  if (st != DEV_OK) {
    snprintf(d, n, "bus=I2C1,addr=0x%02X,who=0x%02X,err=%s", m->addr, m->who_am_i, dev_status_str(st));
    verdict(hw, r, "MPU6050", 0, d);
    return;
  }
  mpu6050_data_t v;
  mpu6050_convert(&raw, &v);
  int32_t norm = (int32_t)mpu6050_accel_norm_mg(&v);
  int32_t dev_mg = norm - ST_ACCEL_NORM_MG;
  int ok_a = dev_mg <= ST_ACCEL_TOL_MG && dev_mg >= -ST_ACCEL_TOL_MG;
  int ok_g = 1;
  for (int i = 0; i < 3; i++) {
    if (v.gyro_mdps[i] > ST_GYRO_MAX_MDPS || v.gyro_mdps[i] < -ST_GYRO_MAX_MDPS) {
      ok_g = 0;
    }
  }
  int ok_t = v.temp_cdeg >= ST_TEMP_MIN_CDEG && v.temp_cdeg <= ST_TEMP_MAX_CDEG;
  snprintf(d, n,
           "bus=I2C1,addr=0x%02X,who=0x%02X,acc_mg=%ld/%ld/%ld,norm_mg=%ld,dev_mg=%ld,"
           "gyro_mdps=%ld/%ld/%ld,temp_cdeg=%ld,chk=%c%c%c",
           m->addr, m->who_am_i, (long)v.accel_mg[0], (long)v.accel_mg[1], (long)v.accel_mg[2], (long)norm,
           (long)dev_mg, (long)v.gyro_mdps[0], (long)v.gyro_mdps[1], (long)v.gyro_mdps[2],
           (long)v.temp_cdeg, ok_a ? 'A' : 'a', ok_g ? 'G' : 'g', ok_t ? 'T' : 't');
  verdict(hw, r, "MPU6050", ok_a && ok_g && ok_t, d);
}

/* ---- W25Q64 ------------------------------------------------------------- */
static void check_flash(const app_hw_t *hw, selftest_result_t *r, char *d, size_t n)
{
  w25q64_t *f = hw->flash;
  dev_status_t st = w25q64_probe(f, hw->spi, hw->time);
  const char *stage = "jedec";
  uint8_t wr[ST_FLASH_TEST_LEN];
  uint8_t rd[ST_FLASH_TEST_LEN];
  uint32_t mismatch = 0;
  uint32_t t_erase = 0;

  if (st == DEV_OK) {
    stage = "erase";
    uint32_t t0 = hw->time->millis();
    st = w25q64_sector_erase(f, ST_FLASH_TEST_ADDR);
    t_erase = hw->time->millis() - t0;
  }
  if (st == DEV_OK) {
    stage = "blank";
    st = w25q64_read(f, ST_FLASH_TEST_ADDR, rd, sizeof rd);
    for (uint32_t i = 0; st == DEV_OK && i < sizeof rd; i++) {
      if (rd[i] != 0xFF) {
        st = DEV_ERR_VERIFY;
      }
    }
  }
  if (st == DEV_OK) {
    stage = "program";
    selftest_flash_pattern(wr, sizeof wr, (uint8_t)hw->time->millis());
    st = w25q64_page_program(f, ST_FLASH_TEST_ADDR, wr, sizeof wr);
  }
  if (st == DEV_OK) {
    stage = "readback";
    memset(rd, 0, sizeof rd);
    st = w25q64_read(f, ST_FLASH_TEST_ADDR, rd, sizeof rd);
    for (uint32_t i = 0; i < sizeof rd; i++) {
      mismatch += (rd[i] != wr[i]);
    }
    if (st == DEV_OK && mismatch != 0) {
      st = DEV_ERR_VERIFY;
    }
  }
  snprintf(d, n, "bus=SPI1,jedec=%02X%02X%02X,test_addr=0x%06lX,len=%u,erase_ms=%lu,mismatch=%lu,stage=%s,err=%s",
           f->jedec[0], f->jedec[1], f->jedec[2], (unsigned long)ST_FLASH_TEST_ADDR,
           (unsigned)ST_FLASH_TEST_LEN, (unsigned long)t_erase, (unsigned long)mismatch, stage,
           dev_status_str(st));
  verdict(hw, r, "W25Q64", st == DEV_OK, d);
}

/* ---- SG92R ×2 ----------------------------------------------------------- */
static void check_servo(const app_hw_t *hw, selftest_result_t *r, char *d, size_t n, const char *name, servo_t *s)
{
  static const uint32_t angles[3] = { 0, 180, 90 };
  uint32_t got[3] = { 0 };
  int ok = s->pwm->get_period_us(s->pwm->ctx) == SERVO_PERIOD_US;
  for (int i = 0; i < 3; i++) {
    uint32_t want = servo_set_deg(s, (int32_t)angles[i]);
    got[i] = s->pwm->get_pulse_us(s->pwm->ctx);
    if (got[i] + ST_PULSE_TOL_US < want || got[i] > want + ST_PULSE_TOL_US) {
      ok = 0;
    }
  }
  snprintf(d, n, "pwm=%s,period_us=%lu,pulse_us_0deg=%lu,pulse_us_180deg=%lu,pulse_us_90deg=%lu", name,
           (unsigned long)s->pwm->get_period_us(s->pwm->ctx), (unsigned long)got[0], (unsigned long)got[1],
           (unsigned long)got[2]);
  verdict(hw, r, s == hw->servo_pan ? "SERVO_PAN" : "SERVO_TILT", ok, d);
}

/* ---- LED ×2, B1, FT232RL ----------------------------------------------- */
static void check_leds(const app_hw_t *hw, selftest_result_t *r, char *d, size_t n)
{
  static const char *const names[APP_LED_COUNT] = { "LED_LIVE", "LED_REC" };
  for (int i = 0; i < APP_LED_COUNT; i++) {
    hw->led_set((app_led_t)i, true);
    int on = hw->led_get((app_led_t)i);
    hw->led_set((app_led_t)i, false);
    int off = !hw->led_get((app_led_t)i);
    snprintf(d, n, "readback_on=%d,readback_off=%d", on, off);
    verdict(hw, r, names[i], on && off, d);
  }
}

static void check_button(const app_hw_t *hw, selftest_result_t *r, char *d, size_t n)
{
  int pressed = hw->button_pressed();
  snprintf(d, n, "pin=PC13,idle=%s", pressed ? "pressed" : "released");
  verdict(hw, r, "BTN_B1", !pressed, d);
}

static void check_ft232(const app_hw_t *hw, selftest_result_t *r, char *d, size_t n)
{
  dev_status_t st = app_print_aux(hw, "FT232RL link check: %s %s\r\n", hw->board_name, hw->fw_version);
  snprintf(d, n, "uart=USART1,baud=115200,tx=%s", dev_status_str(st));
  verdict(hw, r, "FT232RL", st == DEV_OK, d);
}

selftest_result_t selftest_run(const app_hw_t *hw)
{
  selftest_result_t r = { 0, 0 };
  char d[128];
  hw->mark(true);
  app_print(hw, "ST,dev=BEGIN,fw=%s,board=%s\r\n", hw->fw_version, hw->board_name);
  check_mcu(hw, &r, d, sizeof d);
  check_mpu(hw, &r, d, sizeof d);
  check_flash(hw, &r, d, sizeof d);
  check_servo(hw, &r, d, sizeof d, "TIM3_CH1", hw->servo_pan);
  check_servo(hw, &r, d, sizeof d, "TIM3_CH2", hw->servo_tilt);
  check_leds(hw, &r, d, sizeof d);
  check_button(hw, &r, d, sizeof d);
  check_ft232(hw, &r, d, sizeof d);
  app_print(hw, "ST,dev=SUMMARY,res=%s,pass=%u,fail=%u\r\n", r.fail ? "FAIL" : "PASS", r.pass, r.fail);
  hw->mark(false);
  return r;
}
