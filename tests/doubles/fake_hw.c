#include "fake_hw.h"

#include <string.h>

#include "fake_time.h"
#include "selftest.h"

char fake_con[8192];
size_t fake_con_len;
char fake_aux[512];
int fake_leds[APP_LED_COUNT];
int fake_led_stuck[APP_LED_COUNT];
int fake_button;
int fake_mark_edges;
const char *fake_con_input;
int fake_aux_fail;
int fake_recovered;

static uint32_t pulse[2];
static void pwm_set(void *ctx, uint32_t us) { pulse[(intptr_t)ctx] = us; }
static uint32_t pwm_get(void *ctx) { return pulse[(intptr_t)ctx]; }
static uint32_t pwm_period(void *ctx) { (void)ctx; return 20000; }
static const pwm_out_t pwm_pan = { pwm_set, pwm_get, pwm_period, (void *)0 };
static const pwm_out_t pwm_tilt = { pwm_set, pwm_get, pwm_period, (void *)1 };

static void led_set(app_led_t l, bool on)
{
  if (!fake_led_stuck[l]) {
    fake_leds[l] = on;
  }
}
static bool led_get(app_led_t l) { return fake_leds[l] != 0; }
static bool button_pressed(void) { return fake_button != 0; }
static void mark(bool level) { fake_mark_edges += level ? 1 : 0; }

static void con_write(const char *s, size_t len)
{
  if (fake_con_len + len < sizeof fake_con) {
    memcpy(&fake_con[fake_con_len], s, len);
    fake_con_len += len;
    fake_con[fake_con_len] = '\0';
  }
}

static int con_getc(void)
{
  if (fake_con_input == NULL || *fake_con_input == '\0') {
    return -1;
  }
  return (unsigned char)*fake_con_input++;
}

static dev_status_t aux_write(const char *s, size_t len)
{
  if (fake_aux_fail) {
    return DEV_ERR_TIMEOUT;
  }
  size_t n = len < sizeof fake_aux - 1 ? len : sizeof fake_aux - 1;
  memcpy(fake_aux, s, n);
  fake_aux[n] = '\0';
  return DEV_OK;
}

static dev_status_t mem_read_restart(uint8_t addr7, uint8_t reg, uint8_t *val)
{
  (void)addr7;
  (void)reg;
  *val = 0;
  return DEV_ERR_TIMEOUT; /* як у Wokwi: repeated START не підтримується */
}

static void recover(void) { fake_recovered++; }

void fake_hw_init(fake_hw_t *f)
{
  static const uint8_t rest[14] = { 0, 0, 0, 0, 0x40, 0x00, 0xEF, 0x5C, 0, 0, 0, 0, 0, 0 };
  memset(f, 0, sizeof *f);
  fake_con_len = 0;
  fake_con[0] = '\0';
  fake_aux[0] = '\0';
  memset(fake_leds, 0, sizeof fake_leds);
  memset(fake_led_stuck, 0, sizeof fake_led_stuck);
  fake_button = 0;
  fake_mark_edges = 0;
  fake_con_input = NULL;
  fake_aux_fail = 0;
  fake_recovered = 0;
  fake_time_reset();

  fake_i2c_init_mpu6050(&f->i2c_fake);
  fake_i2c_set_burst(&f->i2c_fake, rest);
  f->i2c = fake_i2c_bus(&f->i2c_fake);
  fake_spiflash_init(&f->flash_fake, ST_FLASH_TEST_ADDR);
  f->spi = fake_spiflash_bus(&f->flash_fake);
  servo_init(&f->pan, &pwm_pan, SERVO_DEFAULT_MIN_US, SERVO_DEFAULT_MAX_US);
  servo_init(&f->tilt, &pwm_tilt, SERVO_DEFAULT_MIN_US, SERVO_DEFAULT_MAX_US);

  f->hw = (app_hw_t){
    .i2c = &f->i2c, .spi = &f->spi, .time = &fake_time,
    .mpu = &f->mpu, .flash = &f->flash, .servo_pan = &f->pan, .servo_tilt = &f->tilt,
    .led_set = led_set, .led_get = led_get, .button_pressed = button_pressed, .mark = mark,
    .con_write = con_write, .con_getc = con_getc, .aux_write = aux_write,
    .i2c_mem_read_restart = mem_read_restart, .i2c_recover = recover,
    .sysclk_hz = 48000000u, .board_name = "FAKE", .fw_version = "test",
  };
}

int fake_con_count(const char *needle)
{
  int n = 0;
  size_t l = strlen(needle);
  for (const char *p = fake_con; (p = strstr(p, needle)) != NULL; p += l) {
    n++;
  }
  return n;
}
