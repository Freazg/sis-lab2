/**
 * @file    fake_hw.h
 * @brief   Повний fake app_hw_t для тестів App/: fake I2C (MPU-6050),
 *          fake SPI-flash (W25Q64), fake ШІМ, LED, кнопка, консоль у буфер.
 */
#ifndef FAKE_HW_H
#define FAKE_HW_H

#include "app_hw.h"
#include "fake_i2c.h"
#include "fake_spiflash.h"

typedef struct {
  app_hw_t hw;
  fake_i2c_t i2c_fake;
  i2c_bus_t i2c;
  fake_spiflash_t flash_fake;
  spi_bus_t spi;
  mpu6050_t mpu;
  w25q64_t flash;
  servo_t pan;
  servo_t tilt;
} fake_hw_t;

extern char fake_con[8192];
extern size_t fake_con_len;
extern char fake_aux[512];
extern int fake_leds[APP_LED_COUNT];
extern int fake_led_stuck[APP_LED_COUNT]; /**< 1 — вивід не змінюється (обрив) */
extern int fake_button;
extern int fake_mark_edges;
extern const char *fake_con_input;
extern int fake_aux_fail;
extern int fake_recovered;

/** Налаштувати стенд «усе справне»: MPU у спокої (1 g по Z, 24 °C), флеш відповідає EF4017. */
void fake_hw_init(fake_hw_t *f);

/** Скільки разів підрядок зустрічається у виводі консолі. */
int fake_con_count(const char *needle);

#endif /* FAKE_HW_H */
