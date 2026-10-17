/**
 * @file    cli.h
 * @brief   Розбір команд консолі (чиста логіка, без вводу/виводу).
 *
 * Команди (одна на рядок, регістр не важливий):
 *   help | selftest | status
 *   mode live|hold
 *   servo pan|tilt <0..180>
 *   cap i2c|spi|pwm|uart|anomaly   — відтворювані транзакції для логічного аналізатора
 */
#ifndef CLI_H
#define CLI_H

#include <stddef.h>
#include <stdint.h>

#define CLI_LINE_MAX 48

typedef enum {
  CLI_NONE = 0,
  CLI_HELP,
  CLI_SELFTEST,
  CLI_STATUS,
  CLI_MODE_LIVE,
  CLI_MODE_HOLD,
  CLI_SERVO,
  CLI_CAP_I2C,
  CLI_CAP_SPI,
  CLI_CAP_PWM,
  CLI_CAP_UART,
  CLI_CAP_ANOMALY,
  CLI_ERROR
} cli_cmd_id_t;

typedef struct {
  cli_cmd_id_t id;
  uint8_t servo_index; /**< 0 = pan, 1 = tilt */
  int32_t value;
} cli_cmd_t;

/** Розібрати один рядок (без '\n'). Порожній рядок -> CLI_NONE. */
cli_cmd_t cli_parse(const char *line);

/** Накопичувач рядка: повертає 1, коли рядок завершено ('\r' або '\n'). */
typedef struct {
  char buf[CLI_LINE_MAX];
  size_t len;
  uint8_t overflow;
} cli_reader_t;

int cli_feed(cli_reader_t *r, char c);

#endif /* CLI_H */
