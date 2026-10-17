#include "mock_spi.h"

#include <string.h>

void mock_spi_init(mock_spi_t *m)
{
  memset(m, 0, sizeof *m);
  m->default_resp = 0xFF;
  m->sr1_after_wren = 0x02;
}

static void select_cs(void *ctx, bool active)
{
  mock_spi_t *m = ctx;
  if (active) {
    if (m->open || m->count >= MOCK_SPI_MAX_TX) {
      m->cs_errors++;
      return;
    }
    m->open = 1;
    m->txn[m->count].len = 0;
    m->rx_pos = 0;
  } else {
    if (!m->open) {
      m->cs_errors++;
      return;
    }
    m->last_was_wren = (m->txn[m->count].len == 1 && m->txn[m->count].mosi[0] == 0x06);
    m->open = 0;
    m->count++;
  }
}

static uint8_t next_miso(mock_spi_t *m)
{
  mock_spi_txn_t *t = &m->txn[m->count];
  /* Автоматична модель статус-регістра для команди 05h */
  if (t->len >= 1 && t->mosi[0] == 0x05) {
    if (m->last_was_wren) {
      return m->sr1_after_wren; /* одразу після WREN: WEL */
    }
    if (m->busy_polls > 0) {
      m->busy_polls--;
      return 0x01; /* BUSY */
    }
    return 0x00;
  }
  if (m->resp[m->count] != NULL && m->rx_pos < m->resp_len[m->count]) {
    return m->resp[m->count][m->rx_pos++];
  }
  return m->default_resp;
}

static dev_status_t transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
  mock_spi_t *m = ctx;
  m->transfers++;
  if (!m->open) {
    m->cs_errors++;
    return DEV_ERR_BUS;
  }
  if (m->fail_on_transfer == m->transfers) {
    return DEV_ERR_BUS;
  }
  mock_spi_txn_t *t = &m->txn[m->count];
  int header_done = t->len > 0;
  for (size_t i = 0; i < len; i++) {
    uint8_t out = tx ? tx[i] : 0xFF;
    if (t->len < MOCK_SPI_MAX_BYTES) {
      t->mosi[t->len++] = out;
    }
    if (rx) {
      rx[i] = header_done || i > 0 ? next_miso(m) : m->default_resp;
    }
  }
  return DEV_OK;
}

spi_bus_t mock_spi_bus(mock_spi_t *m)
{
  spi_bus_t b = { select_cs, transfer, m };
  return b;
}
