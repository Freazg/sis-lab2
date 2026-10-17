/**
 * @file    app_hw.h
 * @brief   Усе, що прикладний шар знає про «залізо»: драйвери пристроїв і
 *          кілька функцій плати. Заповнюється в Core/main.c (composition root)
 *          з об'єктів Platform/, у тестах — fake-реалізаціями.
 *          App/ не включає жодного заголовка HAL чи Platform/.
 */
#ifndef APP_HW_H
#define APP_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bus.h"
#include "mpu6050.h"
#include "servo.h"
#include "w25q64.h"

typedef enum { APP_LED_LIVE = 0, APP_LED_REC = 1, APP_LED_COUNT } app_led_t;

typedef struct {
  /* шини (потрібні для демонстраційних транзакцій cap/anomaly) */
  const i2c_bus_t *i2c;
  const spi_bus_t *spi;
  const dev_time_t *time;

  /* драйвери пристроїв PRD */
  mpu6050_t *mpu;
  w25q64_t *flash;
  servo_t *servo_pan;
  servo_t *servo_tilt;

  /* GPIO плати */
  void (*led_set)(app_led_t led, bool on);
  bool (*led_get)(app_led_t led);     /**< фактичний рівень на виводі (IDR) */
  bool (*button_pressed)(void);       /**< B1 USER, true = натиснута */
  void (*mark)(bool level);           /**< службовий GPIO для логічного аналізатора */

  /* консоль (USART2 / ST-LINK VCP) і резервний канал (USART1 / FT232RL) */
  void (*con_write)(const char *s, size_t len);
  int (*con_getc)(void);              /**< -1, якщо байта немає */
  dev_status_t (*aux_write)(const char *s, size_t len);

  /** Читання регістра через HAL_I2C_Mem_Read (repeated START) — для демонстрації аномалії. */
  dev_status_t (*i2c_mem_read_restart)(uint8_t addr7, uint8_t reg, uint8_t *val);
  /** Відновлення I2C після зависання шини (переініціалізація периферії). */
  void (*i2c_recover)(void);

  uint32_t sysclk_hz;
  const char *board_name;
  const char *fw_version;
} app_hw_t;

#endif /* APP_HW_H */
