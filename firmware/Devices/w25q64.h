/**
 * @file    w25q64.h
 * @brief   Драйвер Winbond W25Q64JV (SPI NOR, 64 Mbit). Залежить лише від bus.h.
 *
 * Джерело: W25Q64JV datasheet Revision J (27.03.2018).
 */
#ifndef W25Q64_H
#define W25Q64_H

#include <stddef.h>
#include <stdint.h>

#include "bus.h"

#define W25Q_CMD_WRITE_ENABLE   0x06u /* §8.2 p.24 */
#define W25Q_CMD_WRITE_DISABLE  0x04u
#define W25Q_CMD_READ_SR1       0x05u /* p.25 */
#define W25Q_CMD_READ_DATA      0x03u /* p.28 */
#define W25Q_CMD_PAGE_PROGRAM   0x02u /* p.35 */
#define W25Q_CMD_SECTOR_ERASE   0x20u /* p.37 */
#define W25Q_CMD_JEDEC_ID       0x9Fu /* p.49 */

#define W25Q_SR1_BUSY 0x01u /* S0 */
#define W25Q_SR1_WEL  0x02u /* S1 */

#define W25Q_MANUFACTURER_ID 0xEFu   /* p.21 */
#define W25Q64_DEVICE_ID     0x4017u /* p.21: ID15-ID0 via 9Fh */

#define W25Q_PAGE_SIZE     256u
#define W25Q_SECTOR_SIZE   4096u
#define W25Q64_SIZE        (8u * 1024u * 1024u)

/* Максимальні часи з §9.6 AC Electrical Characteristics p.64 */
#define W25Q_T_PP_MAX_MS   3u
#define W25Q_T_SE_MAX_MS   400u

typedef struct {
  const spi_bus_t *bus;
  const dev_time_t *time;
  uint8_t jedec[3];
} w25q64_t;

/** Прочитати JEDEC ID (9Fh) і перевірити EF 40 17. */
dev_status_t w25q64_probe(w25q64_t *dev, const spi_bus_t *bus, const dev_time_t *time);

dev_status_t w25q64_read_jedec(w25q64_t *dev, uint8_t id[3]);
dev_status_t w25q64_read_sr1(w25q64_t *dev, uint8_t *sr1);

/** 06h, потім контроль WEL=1 через 05h. */
dev_status_t w25q64_write_enable(w25q64_t *dev);

/** Опитувати BUSY, доки не скинеться, не довше timeout_ms. */
dev_status_t w25q64_wait_ready(w25q64_t *dev, uint32_t timeout_ms);

dev_status_t w25q64_read(w25q64_t *dev, uint32_t addr, uint8_t *buf, size_t len);

/** WREN -> 02h + адреса + дані -> очікування BUSY (≤ tPP max). Без перетину межі сторінки. */
dev_status_t w25q64_page_program(w25q64_t *dev, uint32_t addr, const uint8_t *data, size_t len);

/** WREN -> 20h + адреса -> очікування BUSY (≤ tSE max). */
dev_status_t w25q64_sector_erase(w25q64_t *dev, uint32_t addr);

#endif /* W25Q64_H */
