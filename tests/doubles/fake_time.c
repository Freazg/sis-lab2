#include "fake_time.h"

uint32_t fake_time_now_ms;
uint32_t fake_time_step_ms;
uint32_t fake_time_total_delay_ms;

static uint32_t millis(void)
{
  uint32_t t = fake_time_now_ms;
  fake_time_now_ms += fake_time_step_ms;
  return t;
}

static void delay_ms(uint32_t ms)
{
  fake_time_now_ms += ms;
  fake_time_total_delay_ms += ms;
}

void fake_time_reset(void)
{
  fake_time_now_ms = 0;
  fake_time_step_ms = 1;
  fake_time_total_delay_ms = 0;
}

const dev_time_t fake_time = { millis, delay_ms };
