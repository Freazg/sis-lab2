/**
 * @file    mpu6050.h
 * @brief   Драйвер InvenSense MPU-6050 (I2C). Залежить лише від bus.h.
 *
 * Джерела:
 *  [PS] PS-MPU-6000A-00 Product Specification Rev 3.1
 *  [RM] RM-MPU-6000A-00 Register Map and Descriptions Rev 4.0
 */
#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>

#include "bus.h"

#define MPU6050_ADDR_AD0_LOW   0x68u /* [PS] §9.2 p.36: b110100X */
#define MPU6050_ADDR_AD0_HIGH  0x69u

#define MPU6050_REG_SMPLRT_DIV   0x19u /* [RM] Reg 25 p.11 */
#define MPU6050_REG_CONFIG       0x1Au /* [RM] Reg 26 p.13 */
#define MPU6050_REG_GYRO_CONFIG  0x1Bu /* [RM] Reg 27 p.14 */
#define MPU6050_REG_ACCEL_CONFIG 0x1Cu /* [RM] Reg 28 p.15 */
#define MPU6050_REG_ACCEL_XOUT_H 0x3Bu /* [RM] Reg 59 p.29 */
#define MPU6050_REG_PWR_MGMT_1   0x6Bu /* [RM] Reg 107 p.41 */
#define MPU6050_REG_WHO_AM_I     0x75u /* [RM] Reg 117 p.46 */

#define MPU6050_WHO_AM_I_VALUE   0x68u /* [RM] p.46: default 0x68 */

#define MPU6050_ACCEL_LSB_PER_G      16384 /* [PS] §6.2 p.13, AFS_SEL=0 (±2 g) */
#define MPU6050_GYRO_LSB_PER_DPS_X10 1310  /* [PS] §6.1 p.12, FS_SEL=0: 131 LSB/(°/s) */
#define MPU6050_BURST_LEN            14u   /* ACCEL(6) + TEMP(2) + GYRO(6), Reg 59..72 */

typedef struct {
  int16_t accel[3];
  int16_t temp;
  int16_t gyro[3];
} mpu6050_raw_t;

/** Значення у фізичних одиницях (цілі, щоб не тягнути float на Cortex-M0+). */
typedef struct {
  int32_t accel_mg[3];   /**< мілі-g */
  int32_t gyro_mdps[3];  /**< мілі-градусів/с */
  int32_t temp_cdeg;     /**< соті частки °C */
} mpu6050_data_t;

typedef struct {
  const i2c_bus_t *bus;
  const dev_time_t *time;
  uint8_t addr;
  uint8_t who_am_i;     /**< останнє прочитане значення WHO_AM_I */
} mpu6050_t;

/** Прочитати WHO_AM_I. DEV_ERR_ID, якщо значення не 0x68. */
dev_status_t mpu6050_probe(mpu6050_t *dev, const i2c_bus_t *bus, const dev_time_t *time, uint8_t addr);

/**
 * Мінімальна ініціалізація до робочого режиму:
 * DEVICE_RESET -> очікування старту (≤100 мс, [PS] §6.3 p.14) ->
 * PWR_MGMT_1 = 0x01 (тактування від PLL гіроскопа X, рекомендація [RM] p.41) ->
 * CONFIG = 0x03 (DLPF 44 Гц), SMPLRT_DIV = 4 (1 кГц / 5 = 200 Гц),
 * GYRO_CONFIG = 0 (±250 °/с), ACCEL_CONFIG = 0 (±2 g); контрольне читання PWR_MGMT_1.
 */
dev_status_t mpu6050_init(mpu6050_t *dev);

/** Пакетне читання 14 байт з ACCEL_XOUT_H (одна транзакція). */
dev_status_t mpu6050_read_raw(mpu6050_t *dev, mpu6050_raw_t *raw);

/** Розібрати 14 сирих байт (big-endian) у структуру. */
void mpu6050_parse(const uint8_t bytes[MPU6050_BURST_LEN], mpu6050_raw_t *raw);

/* Перетворення в фізичні одиниці — чисті функції, тестуються на хості. */
int32_t mpu6050_accel_mg(int16_t raw);
int32_t mpu6050_gyro_mdps(int16_t raw);
int32_t mpu6050_temp_cdeg(int16_t raw); /* [RM] p.31: T = raw/340 + 36.53 */
void mpu6050_convert(const mpu6050_raw_t *raw, mpu6050_data_t *out);

/** Модуль вектора прискорення, мілі-g (цілочисловий корінь). */
uint32_t mpu6050_accel_norm_mg(const mpu6050_data_t *d);

#endif /* MPU6050_H */
