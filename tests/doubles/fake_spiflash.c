#include "fake_spiflash.h"

#include <string.h>

void fake_spiflash_init(fake_spiflash_t *f, uint32_t sector_base)
{
  memset(f, 0, sizeof *f);
  f->jedec[0] = 0xEF;
  f->jedec[1] = 0x40;
  f->jedec[2] = 0x17;
  f->sector_base = sector_base;
  f->corrupt_bit = -1;
  memset(f->sector, 0xA0, sizeof f->sector); /* «старі» дані, поки сектор не стерто */
}

static uint32_t addr(const fake_spiflash_t *f)
{
  return (uint32_t)f->buf[1] << 16 | (uint32_t)f->buf[2] << 8 | f->buf[3];
}

static uint8_t mem_rd(const fake_spiflash_t *f, uint32_t a)
{
  if (a >= f->sector_base && a < f->sector_base + sizeof f->sector) {
    return f->sector[a - f->sector_base];
  }
  return 0xFF;
}

static void select_cs(void *ctx, bool active)
{
  fake_spiflash_t *f = ctx;
  if (active) {
    f->active = 1;
    f->len = 0;
    return;
  }
  f->active = 0;
  if (f->len == 0) {
    return;
  }
  switch (f->buf[0]) {
    case 0x06:
      f->wel = 1;
      break;
    case 0x02:
      if (f->wel && f->len > 4) {
        uint32_t a = addr(f);
        for (uint16_t i = 4; i < f->len; i++) {
          uint32_t p = (a & ~0xFFu) | ((a + i - 4u) & 0xFFu);
          if (p >= f->sector_base && p < f->sector_base + sizeof f->sector) {
            f->sector[p - f->sector_base] &= f->buf[i];
          }
        }
      }
      f->wel = 0;
      break;
    case 0x20:
      if (f->wel && (addr(f) & ~0xFFFu) == f->sector_base) {
        memset(f->sector, 0xFF, sizeof f->sector);
      }
      f->wel = 0;
      break;
    default:
      break;
  }
}

static dev_status_t transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n)
{
  fake_spiflash_t *f = ctx;
  if (!f->active) {
    return DEV_ERR_BUS;
  }
  for (size_t i = 0; i < n; i++) {
    uint8_t out = 0xFF;
    uint16_t pos = f->len;
    if (pos > 0) {
      switch (f->buf[0]) {
        case 0x9F: out = pos <= 3 ? f->jedec[pos - 1] : 0xFF; break;
        case 0x05: out = f->wel ? 0x02 : 0x00; break;
        case 0x03:
          if (pos >= 4) {
            out = mem_rd(f, addr(f) + pos - 4u);
            if (pos == 4 && f->corrupt_bit >= 0) {
              out ^= (uint8_t)(1u << f->corrupt_bit);
            }
          }
          break;
        default: break;
      }
    }
    if (f->len < sizeof f->buf) {
      f->buf[f->len++] = tx ? tx[i] : 0xFF;
    }
    if (rx) {
      rx[i] = out;
    }
  }
  return DEV_OK;
}

spi_bus_t fake_spiflash_bus(fake_spiflash_t *f)
{
  spi_bus_t b = { select_cs, transfer, f };
  return b;
}
