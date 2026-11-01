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

typedef enum {
  APP_PROBE_IMU = 0,  /**< PA0: задача IMU, спад = «IMU прочитано» (FR-11) */
  APP_PROBE_PWM = 1,  /**< PA1: імпульс навколо запису CCR, «PWM оновлено» (FR-11) */
  APP_PROBE_LONG = 2, /**< PA4: довга операція (flash, dump) */
  APP_PROBE_REACT = 3 /**< PB1: реакція на подію кнопки */
} app_probe_t;

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

  /* вимірювання (ЛР3) */
  uint32_t (*cycles)(void);           /**< лічильник тактів 32 біт, різниця коректна до 2^32 тактів */
  uint32_t (*micros)(void);           /**< мітка часу МК для логів EVT */
  uint32_t (*cal_loop)(void);         /**< такти на еталонний цикл: калібрування швидкості ядра */
  void (*probe)(unsigned n, bool level);
  bool (*btn_take)(uint32_t *t_cycles);
  bool (*rx_eol_take)(uint32_t *t_cycles);
  uint32_t (*rx_dropped)(void);
  uint32_t (*stack_used)(void);
  uint32_t (*stack_size)(void);

  /* сценарії відмов (методичка §6.2) */
  void (*reset)(void);
  void (*boot_fault_set)(uint32_t ms);
  void (*i2c_fault)(unsigned mode);

  uint32_t sysclk_hz;
  const char *board_name;
  const char *fw_version;
  const char *fw_git;
} app_hw_t;

#endif /* APP_HW_H */
