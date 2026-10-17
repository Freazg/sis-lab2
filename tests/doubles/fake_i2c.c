#include "fake_i2c.h"

#include <string.h>

static void log_op(fake_i2c_t *f, uint8_t addr, uint8_t rd, uint8_t reg, size_t len)
{
  if (f->log_len < FAKE_I2C_LOG_MAX) {
    f->log[f->log_len++] = (fake_i2c_op_t){ addr, rd, reg, (uint8_t)len };
  }
}

static dev_status_t check(fake_i2c_t *f, uint8_t addr)
{
  f->calls++;
  if (f->fail_on_call == f->calls) {
    return f->fail_status;
  }
  return addr == f->dev_addr ? DEV_OK : DEV_ERR_NACK;
}

static dev_status_t f_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
  fake_i2c_t *f = ctx;
  dev_status_t st = check(f, addr7);
  log_op(f, addr7, 0, len ? data[0] : 0, len);
  if (st != DEV_OK || len == 0) {
    return st;
  }
  uint8_t reg = data[0];
  for (size_t i = 1; i < len; i++) {
    f->regs[(reg + i - 1) & 0x7F] = data[i];
  }
  /* DEVICE_RESET: конфігураційні регістри -> значення скидання, SLEEP=1.
   * Регістри виходу 0x3B..0x48 відображають фізичну величину і зберігаються. */
  if (reg == 0x6B && len >= 2 && (data[1] & 0x80)) {
    uint8_t keep[14];
    uint8_t who = f->regs[0x75];
    memcpy(keep, &f->regs[0x3B], sizeof keep);
    memset(f->regs, 0, sizeof f->regs);
    memcpy(&f->regs[0x3B], keep, sizeof keep);
    f->regs[0x6B] = 0x40;
    f->regs[0x75] = who;
  }
  return DEV_OK;
}

static dev_status_t f_write_read(void *ctx, uint8_t addr7, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
  fake_i2c_t *f = ctx;
  dev_status_t st = check(f, addr7);
  log_op(f, addr7, 1, wlen ? w[0] : 0, rlen);
  if (st != DEV_OK) {
    return st;
  }
  uint8_t reg = wlen ? w[0] : 0;
  for (size_t i = 0; i < rlen; i++) {
    r[i] = f->regs[(reg + i) & 0x7F];
  }
  return DEV_OK;
}

static dev_status_t f_probe(void *ctx, uint8_t addr7)
{
  return check(ctx, addr7);
}

void fake_i2c_init_mpu6050(fake_i2c_t *f)
{
  memset(f, 0, sizeof *f);
  f->dev_addr = 0x68;
  f->regs[0x6B] = 0x40; /* PWR_MGMT_1 reset value */
  f->regs[0x75] = 0x68; /* WHO_AM_I */
}

void fake_i2c_set_burst(fake_i2c_t *f, const uint8_t bytes[14])
{
  memcpy(&f->regs[0x3B], bytes, 14);
}

i2c_bus_t fake_i2c_bus(fake_i2c_t *f)
{
  i2c_bus_t b = { f_write, f_write_read, f_probe, f };
  return b;
}
