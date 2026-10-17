/**
 * @file    fake_time.h
 * @brief   Fake часу: millis() зростає на step_ms при кожному виклику (щоб цикли
 *          очікування BUSY гарантовано завершувались), delay_ms() додає затримку
 *          і запам'ятовує сумарний час очікування.
 */
#ifndef FAKE_TIME_H
#define FAKE_TIME_H

#include <stdint.h>

#include "bus.h"

extern uint32_t fake_time_now_ms;
extern uint32_t fake_time_step_ms;
extern uint32_t fake_time_total_delay_ms;

void fake_time_reset(void);
extern const dev_time_t fake_time;

#endif /* FAKE_TIME_H */
