/**
 * @file    fake_spiflash.h
 * @brief   FAKE W25Q64 на рівні шини SPI: розуміє 9Fh, 06h, 05h, 03h, 02h, 20h і зберігає
 *          дані одного сектора 4 КБ. Потрібен тестам App/ (самоперевірка пише і читає флеш).
 */
#ifndef FAKE_SPIFLASH_H
#define FAKE_SPIFLASH_H

#include <stdint.h>

#include "bus.h"

typedef struct {
  uint8_t jedec[3];
  uint8_t sector[4096];
  uint32_t sector_base;
  uint8_t wel;
  int corrupt_bit;   /**< >=0: цей біт першого байта при читанні інвертується (дефект) */
  /* стан транзакції */
  int active;
  uint8_t buf[300];
  uint16_t len;
} fake_spiflash_t;

void fake_spiflash_init(fake_spiflash_t *f, uint32_t sector_base);
spi_bus_t fake_spiflash_bus(fake_spiflash_t *f);

#endif /* FAKE_SPIFLASH_H */
