/**
 * @file    mock_spi.h
 * @brief   MOCK шини SPI: записує кожну транзакцію (CS↓ ... CS↑) з байтами MOSI
 *          і видає на MISO заздалегідь задані відповіді. Тест порівнює журнал з
 *          очікуваною послідовністю команд (наприклад, 06h перед 02h).
 *
 *  Відповіді задаються по транзакціях: для N-ї транзакції — масив байтів, які
 *  отримає драйвер у фазі читання (після заголовка команди).
 */
#ifndef MOCK_SPI_H
#define MOCK_SPI_H

#include <stdint.h>

#include "bus.h"

#define MOCK_SPI_MAX_TX     32
#define MOCK_SPI_MAX_BYTES  300

typedef struct {
  uint8_t mosi[MOCK_SPI_MAX_BYTES];
  uint16_t len;
} mock_spi_txn_t;

typedef struct {
  mock_spi_txn_t txn[MOCK_SPI_MAX_TX];
  int count;            /**< кількість завершених транзакцій */
  int open;             /**< CS зараз активний */
  int cs_errors;        /**< transfer без CS або повторний select */
  /* Відповіді MISO: resp[i] для i-ї транзакції; якщо не задано — default_resp. */
  const uint8_t *resp[MOCK_SPI_MAX_TX];
  uint16_t resp_len[MOCK_SPI_MAX_TX];
  uint8_t default_resp;
  uint16_t rx_pos;
  /** Повертати SR1 = busy_polls разів 0x01 (BUSY) на команду 05h, далі 0x00 або wel_value. */
  int busy_polls;
  uint8_t sr1_after_wren; /**< значення SR1 одразу після WREN (WEL=1 -> 0x02) */
  int last_was_wren;
  int fail_on_transfer;   /**< 1-based номер виклику transfer, що поверне DEV_ERR_BUS */
  int transfers;
} mock_spi_t;

void mock_spi_init(mock_spi_t *m);
spi_bus_t mock_spi_bus(mock_spi_t *m);

#endif /* MOCK_SPI_H */
