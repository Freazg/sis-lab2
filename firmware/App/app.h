/**
 * @file    app.h
 * @brief   Прикладна логіка Motion-Mirroring. У ЛР4 кожна функція *_step() — тіло окремої задачі
 *          FreeRTOS (Rtos/rtos_app.c); App сам не знає про ОС і працює через app_hw_t.
 *          Задачі, пріоритети, механізми взаємодії — docs/rtos-design.md.
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>

#include "app_hw.h"
#include "cli.h"
#include "motion.h"
#include "orient.h"
#include "perf.h"
#include "traj.h"

#define APP_IMU_PERIOD_US   5000u  /* FR-01: 200 Гц */
#define APP_IMU_TOL_US      500u   /* FR-01: ±0.5 мс */
#define APP_SERVO_PERIOD_US 20000u /* FR-04: 50 Гц */
#define APP_REC_PERIOD_US   20000u /* FR-07: 50 Гц, семпл бере задача servo */
#define APP_IMU_LOST_MS     100u   /* FR-10 */
#define APP_BTN_RELEASE_MS  20u    /* B1 відпущена стабільно: кінець брязкоту */
#define APP_CAL_SAMPLES     200u   /* 1 с при 200 Гц */
#define APP_I2C_WAIT_MS     2u     /* IMU чекає шину не довше: інакше зсунеться період */

#define APP_TRAJ_BASE    0x000000u
#define APP_TRAJ_SECTORS 16u       /* 64 КБ: 256 сторінок, 10 хв запису при 50 Гц */

typedef enum { APP_MODE_LIVE = 0, APP_MODE_HOLD, APP_MODE_REC, APP_MODE_PLAY } app_mode_t;

/** Лічильники часу. Значення в тактах, вивід — командою stats у форматі методички ЛР3 §5.1. */
typedef enum {
  APP_ST_CLI = 0, APP_ST_BTN, APP_ST_IMU, APP_ST_SERVO, APP_ST_REC, APP_ST_PLAY, APP_ST_LOOP,
  APP_ST_I2C, APP_ST_PROG, APP_ST_ERASE, APP_ST_READ,
  APP_ST_BTN_LAT, APP_ST_CMD_LAT, APP_ST_E2E,
  APP_ST_COUNT
} app_stat_t;

typedef enum {
  APP_EV_BOOT = 0, APP_EV_BTN_PRESS, APP_EV_MODE, APP_EV_ERASE_START, APP_EV_ERASE_DONE,
  APP_EV_PROG_START, APP_EV_PROG_DONE, APP_EV_DUMP_START, APP_EV_DUMP_DONE, APP_EV_IMU_LOST,
  APP_EV_IMU_RESTORED, APP_EV_COUNT
} app_event_t;

#define APP_EVT_LOG 32u /* степінь двійки */

typedef struct {
  uint32_t t_us;
  uint32_t id;
} app_evt_t;

typedef struct {
  const app_hw_t *hw;
  volatile app_mode_t mode; /**< пише лише задача traj, читають усі */
  uint32_t cyc_per_us;

  /* періоди: задачі ОС чекають vTaskDelayUntil, тут — лише заданий період і мітки для джитера */
  uint32_t imu_period_us;
  uint32_t imu_prev, servo_prev;
  uint8_t imu_prev_valid, servo_prev_valid;

  /* IMU */
  motion_t motion;
  int32_t accel_prev[3];
  uint8_t imu_lost;
  uint8_t imu_failing;
  uint8_t boot_reported;
  uint32_t imu_fail_ms;
  uint32_t imu_err;
  uint32_t i2c_busy;   /**< IMU не дочекався шини за APP_I2C_WAIT_MS (ЛР4) */
  uint16_t cal_left;
  int32_t cal_sum[3];
  uint32_t t_motion;
  volatile uint8_t motion_pending;

  /* кнопка */
  uint32_t btn_events;

  /* траєкторія: page — задача traj (REC) або servo (PLAY), dump_page — задача long */
  uint8_t page[TRAJ_PAGE_SIZE];
  uint8_t dump_page[TRAJ_PAGE_SIZE];
  traj_enc_t enc;
  traj_dec_t dec;
  traj_dec_t dump_dec;
  uint32_t traj_addr;
  uint16_t traj_session, traj_seq;
  uint32_t traj_samples;
  uint32_t traj_t_first, traj_t_last;
  uint8_t tear_next_page;
  uint8_t play_done;          /**< кінець траєкторії вже передано задачі traj */
  volatile uint8_t long_busy; /**< dump або erase у задачі long: REC/PLAY чекають кінця */

  /* вимірювання */
  perf_stat_t st[APP_ST_COUNT];
  perf_jitter_t jit_imu, jit_servo;
  uint8_t load_last, load_min, load_max;
  app_evt_t evt[APP_EVT_LOG];
  uint32_t evt_n;

  cli_reader_t cli;
} app_t;

/** Банер, самоперевірка, сканування траєкторії, рядок "READY". До старту планувальника. */
void app_init(app_t *app, const app_hw_t *hw);

/* ---- Тіла задач ------------------------------------------------------------ */
/** cli: обробити всі байти, що надійшли з UART. */
void app_cli_step(app_t *app);
/** btn: реакція на натискання B1 з міткою часу першого спаду (такти). */
void app_btn_event(app_t *app, uint32_t t_event);
/** imu: один запуск з періодом imu_period_us; now — такти одразу після пробудження. */
void app_imu_step(app_t *app, uint32_t now);
/** servo: один запуск з періодом 20 мс; у REC віддає семпл, у PLAY бере наступний. */
void app_servo_step(app_t *app, uint32_t now);
/** traj: семпл із черги — у поточну сторінку; повна сторінка пишеться у флеш. */
void app_rec_add(app_t *app, int16_t pan, int16_t tilt);
/** long: довга операція з черги команд. */
void app_long_run(app_t *app, app_long_op_t op);
/** Вікно завантаження CPU (1 с), рахує ОС за часом idle. */
void app_load_update(app_t *app, uint8_t pct);

/** Виконати розібрану команду (доступно тестам). */
void app_execute(app_t *app, const cli_cmd_t *cmd);

/** Зміна режиму: виконує задача traj (запис і відтворення працюють із флеш). */
void app_set_mode(app_t *app, app_mode_t m);

/** Реакція на коротке натискання B1: LIVE -> HOLD -> REC -> PLAY -> LIVE. */
void app_next_mode(app_t *app);

#endif /* APP_H */
