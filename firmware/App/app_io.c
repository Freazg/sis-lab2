#include "app_io.h"

#include <stdarg.h>
#include <stdio.h>

#define APP_LINE_MAX 160

void app_print(const app_hw_t *hw, const char *fmt, ...)
{
  char buf[APP_LINE_MAX];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n < 0) {
    return;
  }
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
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n < 0) {
    return DEV_ERR_PARAM;
  }
  if ((size_t)n >= sizeof buf) {
    n = (int)sizeof buf - 1;
  }
  return hw->aux_write(buf, (size_t)n);
}
