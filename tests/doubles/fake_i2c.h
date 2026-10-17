/**
 * @file    fake_i2c.h
 * @brief   FAKE шини I2C: спрощена, але робоча модель регістрового файлу пристрою
 *          (MPU-6050). Запис [reg, data...] змінює регістри, write_read читає з
 *          автоінкрементом — як справжній чіп. Підтримує інжекцію помилок
 *          (NACK на адресу, таймаут на N-му виклику) і веде журнал транзакцій.
 *
 *  fake vs mock: fake перевіряє РЕЗУЛЬТАТ (стан регістрів після init),
 *  mock (mock_spi.h) перевіряє ПОСЛІДОВНІСТЬ викликів.
 */
#ifndef FAKE_I2C_H
#define FAKE_I2C_H

#include <stdint.h>

#include "bus.h"

#define FAKE_I2C_LOG_MAX 64

typedef struct {
  uint8_t addr;
  uint8_t is_read;
  uint8_t reg;
  uint8_t len;
} fake_i2c_op_t;

typedef struct {
  uint8_t dev_addr;       /**< адреса, на яку пристрій відповідає ACK */
  uint8_t regs[128];
  int fail_on_call;       /**< 1-based номер виклику, що завершиться fail_status; 0 — ніколи */
  dev_status_t fail_status;
  int calls;
  fake_i2c_op_t log[FAKE_I2C_LOG_MAX];
  int log_len;
} fake_i2c_t;

/** Ініціалізувати модель MPU-6050 зі значеннями скидання ([RM] p.8). */
void fake_i2c_init_mpu6050(fake_i2c_t *f);

/** Заповнити ACCEL/TEMP/GYRO 14 байтами (big-endian), як їх видає чіп. */
void fake_i2c_set_burst(fake_i2c_t *f, const uint8_t bytes[14]);

/** Отримати об'єкт шини, що працює з цим fake. */
i2c_bus_t fake_i2c_bus(fake_i2c_t *f);

#endif /* FAKE_I2C_H */
