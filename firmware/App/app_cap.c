#include "app_internal.h"
#include "app_io.h"
#include "selftest.h"

#define CAP_FLASH_ADDR 0x7FF100u /* сторінка в тестовому секторі ST_FLASH_TEST_ADDR */
#define CAP_FLASH_LEN  16u
#define CAP_PWM_HOLD_MS 100u     /* 5 періодів ШІМ на кожне значення */

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

void app_cap(app_t *a, cli_cmd_id_t id)
{
  static const char *const names[] = { "i2c", "spi", "pwm", "uart", "anomaly" };
  const app_hw_t *hw = a->hw;
  const char *name = names[id - CLI_CAP_I2C];
  app_print(hw, "CAP,%s,BEGIN\r\n", name);
  switch (id) {
    case CLI_CAP_I2C: cap_i2c(hw); break;
    case CLI_CAP_SPI: cap_spi(hw); break;
    case CLI_CAP_PWM: cap_pwm(hw); break;
    case CLI_CAP_UART: cap_uart(hw); break;
    default: cap_anomaly(hw); break;
  }
  app_print(hw, "CAP,%s,END\r\n", name);
}
