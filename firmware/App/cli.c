#include "cli.h"

#include <string.h>

#define MAX_TOKENS 4

/* Власні lower і розбір числа замість ctype/strtol: таблиця _ctype_ і strtol займали 0,7 КБ флеш (ЛР4) */
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static int token_eq(const char *a, const char *b)
{
  while (*a && *b) {
    if (lower(*a) != lower(*b)) {
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

/** Десяткове ціле зі знаком; увесь токен — число, інакше 0. */
static int parse_int(const char *s, int32_t *out)
{
  int neg = *s == '-';
  if (*s == '-' || *s == '+') {
    s++;
  }
  if (*s == '\0') {
    return 0;
  }
  uint32_t v = 0;
  for (; *s != '\0'; s++) {
    if (*s < '0' || *s > '9' || v > 214748364u) {
      return 0;
    }
    v = v * 10u + (uint32_t)(*s - '0');
  }
  *out = neg ? -(int32_t)v : (int32_t)v;
  return 1;
}

/* Команди без числових аргументів: перше слово + необов'язкове друге */
static const struct {
  const char *word;
  const char *arg;
  cli_cmd_id_t id;
} fixed[] = {
  { "help", NULL, CLI_HELP },         { "selftest", NULL, CLI_SELFTEST },
  { "status", NULL, CLI_STATUS },     { "cal", NULL, CLI_CAL },
  { "dump", NULL, CLI_DUMP },         { "erase", NULL, CLI_ERASE },
  { "stats", NULL, CLI_STATS },       { "stats", "reset", CLI_STATS_RESET },
  { "hist", NULL, CLI_HIST },         { "mem", NULL, CLI_MEM },
  { "evt", NULL, CLI_EVT },           { "perfcal", NULL, CLI_PERFCAL },
  { "reset", NULL, CLI_RESET },       { "mode", "live", CLI_MODE_LIVE },
  { "mode", "hold", CLI_MODE_HOLD },  { "mode", "rec", CLI_MODE_REC },
  { "mode", "play", CLI_MODE_PLAY },
  { "cap", "i2c", CLI_CAP_I2C },      { "cap", "spi", CLI_CAP_SPI },
  { "cap", "pwm", CLI_CAP_PWM },      { "cap", "uart", CLI_CAP_UART },
  { "cap", "anomaly", CLI_CAP_ANOMALY },
  { "fault", "powerloss", CLI_FAULT_POWERLOSS },
  { "tasks", NULL, CLI_TASKS },       { "stack", NULL, CLI_STACK },
  { "heap", NULL, CLI_HEAP },         { "ctxsw", NULL, CLI_CTXSW },
  { "trace", NULL, CLI_TRACE },
  { "fault", "invmutex", CLI_FAULT_INV_MUTEX }, { "fault", "invsem", CLI_FAULT_INV_SEM },
  { "fault", "starve", CLI_FAULT_STARVE },      { "fault", "qflood", CLI_FAULT_QFLOOD },
};

cli_cmd_t cli_parse(const char *line)
{
  cli_cmd_t c = { CLI_NONE, 0, 0, 0 };
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
  for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
    if (token_eq(t[0], fixed[i].word)
        && (fixed[i].arg == NULL ? n == 1 : (n == 2 && token_eq(t[1], fixed[i].arg)))) {
      c.id = fixed[i].id;
      return c;
    }
  }
  int32_t v;
  if (n == 3 && token_eq(t[0], "servo")) {
    int which = token_eq(t[1], "pan") ? 0 : (token_eq(t[1], "tilt") ? 1 : -1);
    if (which >= 0 && parse_int(t[2], &v) && v >= 0 && v <= 180) {
      c.id = CLI_SERVO;
      c.servo_index = (uint8_t)which;
      c.value = v;
    }
  } else if (n == 3 && token_eq(t[0], "limits")) {
    int32_t hi;
    if (parse_int(t[1], &v) && parse_int(t[2], &hi)) {
      c.id = CLI_LIMITS;
      c.value = v;
      c.value2 = hi;
    }
  } else if (n == 2 && token_eq(t[0], "rate") && parse_int(t[1], &v)) {
    c.id = CLI_RATE;
    c.value = v;
  }
  return c;
}

int cli_feed(cli_reader_t *r, char ch)
{
  if (r->complete) { /* попередній рядок уже віддано — починаємо новий */
    r->len = 0;
    r->overflow = 0;
    r->complete = 0;
  }
  if (ch == '\r' || ch == '\n') {
    if (r->len == 0 && !r->overflow) {
      return 0; /* порожній рядок або друга половина "\r\n" */
    }
    r->buf[r->len] = '\0';
    r->complete = 1;
    return 1;
  }
  if (r->len + 1 < sizeof r->buf) {
    r->buf[r->len++] = ch;
  } else {
    r->overflow = 1;
  }
  return 0;
}
