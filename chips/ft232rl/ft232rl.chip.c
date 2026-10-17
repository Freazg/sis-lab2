/*
 * Wokwi custom chip: FTDI FT232RL USB-UART bridge (TTL side only).
 *
 * FT232RL — міст USB <-> UART. З боку МК він має лише лінії TXD/RXD
 * (FT232R datasheet v2.10, FT_000053, розд. 3 «Pin Description» p.8).
 * USB-сторону (ПК з терміналом) симулятор не моделює, тому модель:
 *  - приймає байти на RXD (з'єднаний з USART1_TX МК, PB6) на 115200 8N1;
 *  - збирає рядок до '\n' і друкує його в консоль чипів як «FT232RL> ...»
 *    — це те, що побачив би термінал на ПК;
 *  - TXD тримає в стані спокою (high), як на непідключеному терміналі.
 */
#include "wokwi-api.h"
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uart_dev_t uart;
  char line[128];
  uint32_t len;
  uint32_t total;
} chip_t;

static void on_rx(void *user_data, uint8_t byte)
{
  chip_t *c = user_data;
  c->total++;
  if (byte == '\r') {
    return;
  }
  if (byte == '\n' || c->len == sizeof c->line - 1) {
    c->line[c->len] = '\0';
    printf("FT232RL> %s\n", c->line);
    c->len = 0;
    return;
  }
  c->line[c->len++] = (char)byte;
}

void chip_init(void)
{
  static chip_t chip;
  const uart_config_t cfg = {
    .tx = pin_init("TXD", INPUT_PULLUP),
    .rx = pin_init("RXD", INPUT),
    .baud_rate = 115200,
    .rx_data = on_rx,
    .write_done = NULL,
    .user_data = &chip,
  };
  chip.uart = uart_init(&cfg);
  printf("FT232RL model ready (115200 8N1)\n");
}
