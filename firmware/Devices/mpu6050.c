#include "mpu6050.h"

#include <stddef.h>

#include "imath.h"

#define PWR_MGMT_1_DEVICE_RESET 0x80u
#define PWR_MGMT_1_CLKSEL_PLL_X 0x01u
#define MPU6050_STARTUP_MS      100u /* [PS] §6.3 p.14: start-up time for register R/W, max */

static dev_status_t write_reg(mpu6050_t *dev, uint8_t reg, uint8_t value)
{
  const uint8_t buf[2] = { reg, value };
  return dev->bus->write(dev->bus->ctx, dev->addr, buf, sizeof buf);
}

static dev_status_t read_regs(mpu6050_t *dev, uint8_t reg, uint8_t *out, size_t len)
{
  return dev->bus->write_read(dev->bus->ctx, dev->addr, &reg, 1, out, len);
}

dev_status_t mpu6050_probe(mpu6050_t *dev, const i2c_bus_t *bus, const dev_time_t *time, uint8_t addr)
{
  if (dev == NULL || bus == NULL) {
    return DEV_ERR_PARAM;
  }
  dev->bus = bus;
  dev->time = time;
  dev->addr = addr;
  dev->who_am_i = 0;

  dev_status_t st = read_regs(dev, MPU6050_REG_WHO_AM_I, &dev->who_am_i, 1);
  if (st != DEV_OK) {
    return st;
  }
  return dev->who_am_i == MPU6050_WHO_AM_I_VALUE ? DEV_OK : DEV_ERR_ID;
}

dev_status_t mpu6050_init(mpu6050_t *dev)
{
  static const uint8_t cfg[][2] = {
    { MPU6050_REG_PWR_MGMT_1, PWR_MGMT_1_CLKSEL_PLL_X },
    { MPU6050_REG_CONFIG, 0x03 },
    { MPU6050_REG_SMPLRT_DIV, 4 },
    { MPU6050_REG_GYRO_CONFIG, 0x00 },
    { MPU6050_REG_ACCEL_CONFIG, 0x00 },
  };

  dev_status_t st = write_reg(dev, MPU6050_REG_PWR_MGMT_1, PWR_MGMT_1_DEVICE_RESET);
  if (st != DEV_OK) {
    return st;
  }
  if (dev->time != NULL && dev->time->delay_ms != NULL) {
    dev->time->delay_ms(MPU6050_STARTUP_MS);
  }
  for (size_t i = 0; i < sizeof cfg / sizeof cfg[0]; i++) {
    st = write_reg(dev, cfg[i][0], cfg[i][1]);
    if (st != DEV_OK) {
      return st;
    }
  }

  uint8_t pwr = 0xFF;
  st = read_regs(dev, MPU6050_REG_PWR_MGMT_1, &pwr, 1);
  if (st != DEV_OK) {
    return st;
  }
  return pwr == PWR_MGMT_1_CLKSEL_PLL_X ? DEV_OK : DEV_ERR_VERIFY;
}

void mpu6050_parse(const uint8_t b[MPU6050_BURST_LEN], mpu6050_raw_t *raw)
{
  for (int i = 0; i < 3; i++) {
    raw->accel[i] = (int16_t)((uint16_t)b[2 * i] << 8 | b[2 * i + 1]);
    raw->gyro[i] = (int16_t)((uint16_t)b[8 + 2 * i] << 8 | b[8 + 2 * i + 1]);
  }
  raw->temp = (int16_t)((uint16_t)b[6] << 8 | b[7]);
}

dev_status_t mpu6050_read_raw(mpu6050_t *dev, mpu6050_raw_t *raw)
{
  uint8_t b[MPU6050_BURST_LEN];
  dev_status_t st = read_regs(dev, MPU6050_REG_ACCEL_XOUT_H, b, sizeof b);
  if (st == DEV_OK) {
    mpu6050_parse(b, raw);
  }
  return st;
}

int32_t mpu6050_accel_mg(int16_t raw)
{
  return idiv_round((int32_t)raw * 1000, MPU6050_ACCEL_LSB_PER_G);
}

int32_t mpu6050_gyro_mdps(int16_t raw)
{
  /* 131 LSB/(°/s) => mdps = raw * 1000 / 131 = raw * 10000 / 1310 */
  return idiv_round((int32_t)raw * 10000, MPU6050_GYRO_LSB_PER_DPS_X10);
}

int32_t mpu6050_temp_cdeg(int16_t raw)
{
  /* T[°C] = raw / 340 + 36.53  =>  cdeg = raw * 100 / 340 + 3653 */
  return idiv_round((int32_t)raw * 100, 340) + 3653;
}

void mpu6050_convert(const mpu6050_raw_t *raw, mpu6050_data_t *out)
{
  for (int i = 0; i < 3; i++) {
    out->accel_mg[i] = mpu6050_accel_mg(raw->accel[i]);
    out->gyro_mdps[i] = mpu6050_gyro_mdps(raw->gyro[i]);
  }
  out->temp_cdeg = mpu6050_temp_cdeg(raw->temp);
}

uint32_t mpu6050_accel_norm_mg(const mpu6050_data_t *d)
{
  uint64_t s = 0;
  for (int i = 0; i < 3; i++) {
    int64_t v = d->accel_mg[i];
    s += (uint64_t)(v * v);
  }
  return isqrt_u64(s);
}
