/**
 * @file    app_internal.h
 * @brief   Спільне для файлів App/: перемикання режимів, робота з траєкторією, журнал подій.
 */
#ifndef APP_INTERNAL_H
#define APP_INTERNAL_H

#include "app.h"

const char *app_mode_name(app_mode_t m);
void app_set_mode(app_t *a, app_mode_t m);
void app_evt(app_t *a, app_event_t id);
void app_stats_reset(app_t *a);
void app_print_stats(app_t *a);
void app_print_evt(app_t *a);

/* app_traj.c */
typedef struct {
  uint16_t session;
  uint32_t pages;
  uint32_t samples;
  uint8_t torn;  /**< за останньою цілою сторінкою лежить пошкоджена (обрив запису) */
} app_traj_info_t;

void app_traj_scan(app_t *a, app_traj_info_t *out);
bool app_rec_begin(app_t *a);
void app_rec_sample(app_t *a, traj_sample_t s);
void app_rec_end(app_t *a);
bool app_play_begin(app_t *a);
bool app_play_sample(app_t *a);  /**< false — траєкторія закінчилась */
void app_play_end(app_t *a);
void app_traj_dump(app_t *a);
void app_traj_erase(app_t *a);

/* app_cap.c: відтворювані транзакції ЛР2 */
void app_cap(app_t *a, cli_cmd_id_t id);

#endif /* APP_INTERNAL_H */
