#include "app_io.h"

#include <stdarg.h>

/*
 * Власний форматер замість vsnprintf з newlib-nano: той разом із fvwrite, findfp і malloc займав
 * близько 4 КБ із 32 КБ флеш (docs/metrics/memory.md) і тягнув за собою heap. Формати, які тут
 * потрібні: %% %c %s %d %i %u %x %X, модифікатор l, ширина із заповненням нулями або пробілами.
 */
static void put(char *buf, size_t size, size_t *pos, char c)
{
  if (*pos + 1u < size) {
    buf[*pos] = c;
  }
  (*pos)++;
}

static void put_number(char *buf, size_t size, size_t *pos, unsigned long v, unsigned base, int upper, int neg,
                       int width, char pad)
{
  char tmp[sizeof(unsigned long) * 3u]; /* 32 біти: 10 цифр, з запасом під хостовий long */
  int n = 0;
  do {
    unsigned d = (unsigned)(v % base);
    tmp[n++] = (char)(d < 10u ? '0' + d : (upper ? 'A' : 'a') + d - 10u);
    v /= base;
  } while (v != 0u);
  if (neg && pad == '0') {
    put(buf, size, pos, '-');
  }
  for (int w = n + (neg ? 1 : 0); w < width; w++) {
    put(buf, size, pos, pad);
  }
  if (neg && pad != '0') {
    put(buf, size, pos, '-');
  }
  while (n > 0) {
    put(buf, size, pos, tmp[--n]);
  }
}

int app_vformat(char *buf, size_t size, const char *fmt, va_list ap)
{
  size_t pos = 0;
  for (; *fmt != '\0'; fmt++) {
    if (*fmt != '%') {
      put(buf, size, &pos, *fmt);
      continue;
    }
    fmt++;
    char pad = ' ';
    int width = 0;
    int is_long = 0;
    if (*fmt == '0') {
      pad = '0';
      fmt++;
    }
    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + (*fmt++ - '0');
    }
    if (*fmt == 'l') {
      is_long = 1;
      fmt++;
    }
    switch (*fmt) {
      case 'c':
        put(buf, size, &pos, (char)va_arg(ap, int));
        break;
      case 's': {
        const char *s = va_arg(ap, const char *);
        while (*s != '\0') {
          put(buf, size, &pos, *s++);
        }
        break;
      }
      case 'd':
      case 'i': {
        long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
        unsigned long mag = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
        put_number(buf, size, &pos, mag, 10u, 0, v < 0, width, pad);
        break;
      }
      case 'u':
      case 'x':
      case 'X': {
        unsigned long v = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
        put_number(buf, size, &pos, v, *fmt == 'u' ? 10u : 16u, *fmt == 'X', 0, width, pad);
        break;
      }
      case '\0':
        fmt--;
        break;
      default: /* '%' і невідомі специфікатори виводяться як є */
        put(buf, size, &pos, *fmt);
        break;
    }
  }
  if (size > 0u) {
    buf[pos < size ? pos : size - 1u] = '\0';
  }
  return (int)pos;
}

int app_format(char *buf, size_t size, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int n = app_vformat(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

#define APP_LINE_MAX 160

void app_print(const app_hw_t *hw, const char *fmt, ...)
{
  char buf[APP_LINE_MAX];
  va_list ap;
  va_start(ap, fmt);
  int n = app_vformat(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if ((size_t)n >= sizeof buf) {
    n = (int)sizeof buf - 1;
  }
  hw->con_write(buf, (size_t)n);
}

dev_status_t app_print_aux(const app_hw_t *hw, const char *fmt, ...)
{
  char buf[APP_LINE_MAX];
  va_list ap;
  va_start(ap, fmt);
  int n = app_vformat(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if ((size_t)n >= sizeof buf) {
    n = (int)sizeof buf - 1;
  }
  return hw->aux_write(buf, (size_t)n);
}
