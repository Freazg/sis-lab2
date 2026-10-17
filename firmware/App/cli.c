#include "cli.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 4

static int token_eq(const char *a, const char *b)
{
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return 0;
    }
    a++;
    b++;
  }
  return *a == *b;
}

static int split(char *s, char *tok[], int max)
{
  int n = 0;
  while (*s && n < max) {
    while (*s == ' ' || *s == '\t') {
      *s++ = '\0';
    }
    if (*s == '\0') {
      break;
    }
    tok[n++] = s;
    while (*s && *s != ' ' && *s != '\t') {
      s++;
    }
  }
  while (*s == ' ' || *s == '\t') {
    *s++ = '\0';
  }
  return *s ? -1 : n; /* -1: забагато токенів */
}

static int parse_int(const char *s, int32_t *out)
{
  char *end = NULL;
  long v = strtol(s, &end, 10);
  if (end == s || *end != '\0') {
    return 0;
  }
  *out = (int32_t)v;
  return 1;
}

cli_cmd_t cli_parse(const char *line)
{
  cli_cmd_t c = { CLI_NONE, 0, 0 };
  char buf[CLI_LINE_MAX];
  char *t[MAX_TOKENS];
  size_t len = strlen(line);
  if (len >= sizeof buf) {
    c.id = CLI_ERROR;
    return c;
  }
  memcpy(buf, line, len + 1);
  int n = split(buf, t, MAX_TOKENS);
  if (n == 0) {
    return c;
  }
  c.id = CLI_ERROR;
  if (n < 0) {
    return c;
  }
  if (n == 1 && token_eq(t[0], "help")) {
    c.id = CLI_HELP;
  } else if (n == 1 && token_eq(t[0], "selftest")) {
    c.id = CLI_SELFTEST;
  } else if (n == 1 && token_eq(t[0], "status")) {
    c.id = CLI_STATUS;
  } else if (n == 2 && token_eq(t[0], "mode")) {
    if (token_eq(t[1], "live")) {
      c.id = CLI_MODE_LIVE;
    } else if (token_eq(t[1], "hold")) {
      c.id = CLI_MODE_HOLD;
    }
  } else if (n == 3 && token_eq(t[0], "servo")) {
    int32_t v;
    int which = token_eq(t[1], "pan") ? 0 : (token_eq(t[1], "tilt") ? 1 : -1);
    if (which >= 0 && parse_int(t[2], &v) && v >= 0 && v <= 180) {
      c.id = CLI_SERVO;
      c.servo_index = (uint8_t)which;
      c.value = v;
    }
  } else if (n == 2 && token_eq(t[0], "cap")) {
    static const struct {
      const char *name;
      cli_cmd_id_t id;
    } caps[] = {
      { "i2c", CLI_CAP_I2C }, { "spi", CLI_CAP_SPI }, { "pwm", CLI_CAP_PWM },
      { "uart", CLI_CAP_UART }, { "anomaly", CLI_CAP_ANOMALY },
    };
    for (size_t i = 0; i < sizeof caps / sizeof caps[0]; i++) {
      if (token_eq(t[1], caps[i].name)) {
        c.id = caps[i].id;
      }
    }
  }
  return c;
}

int cli_feed(cli_reader_t *r, char ch)
{
  if (ch == '\r' || ch == '\n') {
    if (r->len == 0 && !r->overflow) {
      return 0; /* порожній рядок або друга половина "\r\n" */
    }
    r->buf[r->len] = '\0';
    return 1;
  }
  if (r->len + 1 < sizeof r->buf) {
    r->buf[r->len++] = ch;
  } else {
    r->overflow = 1;
  }
  return 0;
}
