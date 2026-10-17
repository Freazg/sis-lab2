/**
 * @file    bus.h
 * @brief   Абстракції шин, через які драйвери Devices/ працюють із залізом.
 *
 * Драйвер отримує вказівник на структуру з вказівниками на функції і
 * непрозорим контекстом. На платі їх заповнює Platform/ (обгортки над HAL),
 * у хостових тестах — fake/mock із tests/. Драйвер не знає ні про
 * HAL_I2C_Mem_Read, ні про номери пінів.
 */
#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dev_status.h"

/** I2C-контролер. Адреси — 7-бітні (0x68, а не 0xD0). */
typedef struct i2c_bus {
  /** START, addr+W, len байт, STOP. */
  dev_status_t (*write)(void *ctx, uint8_t addr7, const uint8_t *data, size_t len);
  /** Запис wlen байт (номер регістра), потім читання rlen байт. */
  dev_status_t (*write_read)(void *ctx, uint8_t addr7, const uint8_t *wdata, size_t wlen,
                             uint8_t *rdata, size_t rlen);
  /** Перевірка ACK на адресу (START, addr+W, STOP). */
  dev_status_t (*probe)(void *ctx, uint8_t addr7);
  void *ctx;
} i2c_bus_t;

/** SPI-контролер з програмним керуванням CS# (одна лінія CS на пристрій). */
typedef struct spi_bus {
  /** Встановити рівень CS#: true — активний (низький рівень на лінії). */
  void (*select)(void *ctx, bool active);
  /** Повнодуплексний обмін; tx або rx можуть бути NULL. */
  dev_status_t (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
  void *ctx;
} spi_bus_t;

/** Канал ШІМ з фіксованим періодом (для сервоприводу). */
typedef struct pwm_out {
  void (*set_pulse_us)(void *ctx, uint32_t pulse_us);
  uint32_t (*get_pulse_us)(void *ctx); /**< значення, реально записане в регістр порівняння */
  uint32_t (*get_period_us)(void *ctx);
  void *ctx;
} pwm_out_t;

/** Час: потрібен драйверам для очікування BUSY і затримок старту. */
typedef struct dev_time {
  uint32_t (*millis)(void);
  void (*delay_ms)(uint32_t ms);
} dev_time_t;

#endif /* BUS_H */
