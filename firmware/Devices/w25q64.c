#include "w25q64.h"

static dev_status_t xfer(w25q64_t *dev, const uint8_t *tx, uint8_t *rx, size_t len)
{
  return dev->bus->transfer(dev->bus->ctx, tx, rx, len);
}

static void cs(w25q64_t *dev, bool active)
{
  dev->bus->select(dev->bus->ctx, active);
}

/* Одна транзакція з утриманим CS#: команда [+ адреса], потім tx або rx даних. */
static dev_status_t command(w25q64_t *dev, const uint8_t *hdr, size_t hlen,
                            const uint8_t *tx, uint8_t *rx, size_t len)
{
  cs(dev, true);
  dev_status_t st = xfer(dev, hdr, NULL, hlen);
  if (st == DEV_OK && len > 0) {
    st = xfer(dev, tx, rx, len);
  }
  cs(dev, false);
  return st;
}

static void addr24(uint8_t *p, uint32_t addr)
{
  p[0] = (uint8_t)(addr >> 16);
  p[1] = (uint8_t)(addr >> 8);
  p[2] = (uint8_t)addr;
}

dev_status_t w25q64_read_jedec(w25q64_t *dev, uint8_t id[3])
{
  const uint8_t cmd = W25Q_CMD_JEDEC_ID;
  return command(dev, &cmd, 1, NULL, id, 3);
}

dev_status_t w25q64_probe(w25q64_t *dev, const spi_bus_t *bus, const dev_time_t *time)
{
  if (dev == NULL || bus == NULL) {
    return DEV_ERR_PARAM;
  }
  dev->bus = bus;
  dev->time = time;
  dev_status_t st = w25q64_read_jedec(dev, dev->jedec);
  if (st != DEV_OK) {
    return st;
  }
  uint16_t device = (uint16_t)((uint16_t)dev->jedec[1] << 8 | dev->jedec[2]);
  return (dev->jedec[0] == W25Q_MANUFACTURER_ID && device == W25Q64_DEVICE_ID) ? DEV_OK : DEV_ERR_ID;
}

dev_status_t w25q64_read_sr1(w25q64_t *dev, uint8_t *sr1)
{
  const uint8_t cmd = W25Q_CMD_READ_SR1;
  return command(dev, &cmd, 1, NULL, sr1, 1);
}

dev_status_t w25q64_write_enable(w25q64_t *dev)
{
  const uint8_t cmd = W25Q_CMD_WRITE_ENABLE;
  dev_status_t st = command(dev, &cmd, 1, NULL, NULL, 0);
  if (st != DEV_OK) {
    return st;
  }
  uint8_t sr1 = 0;
  st = w25q64_read_sr1(dev, &sr1);
  if (st != DEV_OK) {
    return st;
  }
  return (sr1 & W25Q_SR1_WEL) ? DEV_OK : DEV_ERR_VERIFY;
}

dev_status_t w25q64_wait_ready(w25q64_t *dev, uint32_t timeout_ms)
{
  uint32_t t0 = dev->time->millis();
  for (;;) {
    uint8_t sr1 = 0;
    dev_status_t st = w25q64_read_sr1(dev, &sr1);
    if (st != DEV_OK) {
      return st;
    }
    if ((sr1 & W25Q_SR1_BUSY) == 0) {
      return DEV_OK;
    }
    if ((uint32_t)(dev->time->millis() - t0) > timeout_ms) {
      return DEV_ERR_BUSY;
    }
  }
}

dev_status_t w25q64_read(w25q64_t *dev, uint32_t addr, uint8_t *buf, size_t len)
{
  if (buf == NULL || addr + len > W25Q64_SIZE) {
    return DEV_ERR_PARAM;
  }
  uint8_t hdr[4] = { W25Q_CMD_READ_DATA, 0, 0, 0 };
  addr24(&hdr[1], addr);
  return command(dev, hdr, sizeof hdr, NULL, buf, len);
}

dev_status_t w25q64_page_program(w25q64_t *dev, uint32_t addr, const uint8_t *data, size_t len)
{
  if (data == NULL || len == 0 || len > W25Q_PAGE_SIZE ||
      (addr % W25Q_PAGE_SIZE) + len > W25Q_PAGE_SIZE || addr + len > W25Q64_SIZE) {
    return DEV_ERR_PARAM; /* перетин межі сторінки призвів би до загортання адреси (p.35) */
  }
  dev_status_t st = w25q64_write_enable(dev);
  if (st != DEV_OK) {
    return st;
  }
  uint8_t hdr[4] = { W25Q_CMD_PAGE_PROGRAM, 0, 0, 0 };
  addr24(&hdr[1], addr);
  st = command(dev, hdr, sizeof hdr, data, NULL, len);
  if (st != DEV_OK) {
    return st;
  }
  return w25q64_wait_ready(dev, W25Q_T_PP_MAX_MS);
}

dev_status_t w25q64_sector_erase(w25q64_t *dev, uint32_t addr)
{
  if (addr >= W25Q64_SIZE) {
    return DEV_ERR_PARAM;
  }
  dev_status_t st = w25q64_write_enable(dev);
  if (st != DEV_OK) {
    return st;
  }
  uint8_t hdr[4] = { W25Q_CMD_SECTOR_ERASE, 0, 0, 0 };
  addr24(&hdr[1], addr & ~(W25Q_SECTOR_SIZE - 1u));
  st = command(dev, hdr, sizeof hdr, NULL, NULL, 0);
  if (st != DEV_OK) {
    return st;
  }
  return w25q64_wait_ready(dev, W25Q_T_SE_MAX_MS);
}
