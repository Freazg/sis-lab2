/**
  ******************************************************************************
  * @file    lcd1602.c
  * @brief   LCD1602 (HD44780) у 4-бітному режимі через PCF8574.
  *          Послідовність ініціалізації — HD44780U datasheet, Figure 24
  *          "4-Bit Interface" (Initializing by Instruction).
  ******************************************************************************
  */
#include "lcd1602.h"

#define PCF_RS   0x01U
#define PCF_EN   0x04U
#define PCF_BL   0x08U

#define LCD_TIMEOUT_MS 10U

static I2C_HandleTypeDef *lcd_i2c;

/** Один нібл: E=1, потім E=0 — HD44780 захоплює дані по спаду E. */
static HAL_StatusTypeDef write_nibble(uint8_t nibble, uint8_t rs)
{
  uint8_t base = (uint8_t)((nibble & 0x0FU) << 4) | PCF_BL | (rs ? PCF_RS : 0U);
  uint8_t buf[2] = { (uint8_t)(base | PCF_EN), base };
  return HAL_I2C_Master_Transmit(lcd_i2c, LCD1602_I2C_ADDR << 1, buf, sizeof buf, LCD_TIMEOUT_MS);
}

static HAL_StatusTypeDef write_byte(uint8_t value, uint8_t rs)
{
  HAL_StatusTypeDef st = write_nibble(value >> 4, rs);
  if (st == HAL_OK)
  {
    st = write_nibble(value, rs);
  }
  return st;
}

static HAL_StatusTypeDef command(uint8_t cmd)
{
  HAL_StatusTypeDef st = write_byte(cmd, 0);
  /* Clear (0x01) і Return home (0x02) виконуються до 1.52 мс, решта — 37 мкс. */
  HAL_Delay(cmd <= 0x02U ? 2U : 1U);
  return st;
}

HAL_StatusTypeDef lcd1602_init(I2C_HandleTypeDef *hi2c)
{
  lcd_i2c = hi2c;

  HAL_Delay(50);                       /* > 40 мс після подачі живлення */
  if (write_nibble(0x3, 0) != HAL_OK)  /* Function set (8-bit) x3 */
  {
    return HAL_ERROR;
  }
  HAL_Delay(5);
  write_nibble(0x3, 0);
  HAL_Delay(1);
  write_nibble(0x3, 0);
  HAL_Delay(1);
  write_nibble(0x2, 0);                /* перехід у 4-бітний режим */
  HAL_Delay(1);

  if (command(0x28) != HAL_OK ||       /* 4-bit, 2 рядки, 5x8 */
      command(0x08) != HAL_OK ||       /* дисплей вимкнено */
      command(0x01) != HAL_OK ||       /* очистити */
      command(0x06) != HAL_OK ||       /* курсор вправо, без зсуву */
      command(0x0C) != HAL_OK)         /* дисплей увімкнено, курсор схований */
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}

HAL_StatusTypeDef lcd1602_set_cursor(uint8_t col, uint8_t row)
{
  return command((uint8_t)(0x80U | ((row ? 0x40U : 0x00U) + col)));
}

HAL_StatusTypeDef lcd1602_print(const char *s)
{
  while (*s)
  {
    if (write_byte((uint8_t)*s++, 1) != HAL_OK)
    {
      return HAL_ERROR;
    }
  }
  return HAL_OK;
}
