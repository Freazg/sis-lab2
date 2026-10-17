/**
 * @file    app.h
 * @brief   Прикладна логіка Motion-Mirroring (ЛР2: bring-up + режими LIVE/HOLD + CLI).
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>

#include "app_hw.h"
#include "cli.h"
#include "orient.h"

#define APP_CONTROL_PERIOD_MS 20u /* 50 Гц — частота ШІМ серво */

typedef enum { APP_MODE_LIVE = 0, APP_MODE_HOLD } app_mode_t;

typedef struct {
  const app_hw_t *hw;
  app_mode_t mode;
  uint8_t mpu_ok;
  uint8_t btn_prev;
  uint32_t next_ctrl_ms;
  orient_t orient;
  int32_t pan_deg;
  int32_t tilt_deg;
  cli_reader_t cli;
} app_t;

/** Банер, самоперевірка, рядок "READY". */
void app_init(app_t *app, const app_hw_t *hw);

/** Один неблокуючий крок головного циклу. */
void app_poll(app_t *app);

/** Виконати розібрану команду (доступно тестам). */
void app_execute(app_t *app, const cli_cmd_t *cmd);

/** Обробка фронту натискання B1: LIVE <-> HOLD. */
void app_on_button(app_t *app, uint8_t pressed);

#endif /* APP_H */
