/**
  ******************************************************************************
  * @file    main.c
  * @brief   ЛР1 — bring-up платформи NUCLEO-C031C6 (Wokwi) на STM32 HAL.
  *
  *  Компоненти — рівно з пулу методички для форми B:
  *  MPU-6050 (I2C) + Potentiometer (ADC) + Push button (GPIO) + LCD1602 (I2C),
  *  UART Logger (USART2) і Logic Analyzer.
  *
  *  1. Ініціалізує тактування (HSI48 -> SYSCLK 48 MHz) і периферію:
  *     USART2, I2C1, ADC1, GPIO. Друкує HAL_OK/помилку для кожної ініціалізації.
  *  2. Сканує I2C-шину, читає WHO_AM_I з MPU-6050, ініціалізує LCD1602.
  *  3. У циклі: щосекунди "tick N ..." у UART; кожні 200 мс — нахил
  *     (pitch/roll) з MPU-6050 і значення потенціометра на LCD.
  *     Кнопка B1 перемикає LIVE/HOLD (заморожує показ), LED показує режим.
  ******************************************************************************
  */
#include "main.h"
#include "lcd1602.h"
#include <stdio.h>
#include <math.h>

/* ---- Константи ---------------------------------------------------------- */
#define MPU6050_ADDR          (0x68U << 1)  /* HAL очікує 8-бітну адресу */
#define MPU6050_REG_PWR_MGMT1 0x6BU
#define MPU6050_REG_ACCEL_X_H 0x3BU
#define MPU6050_REG_WHO_AM_I  0x75U
#define MPU6050_WHO_AM_I_VAL  0x68U

#define I2C_TIMEOUT_MS        10U
#define POLL_PERIOD_MS        20U    /* опитування кнопки */
#define DISPLAY_PERIOD_MS     200U   /* оновлення LCD */
#define REPORT_PERIOD_MS      1000U  /* рядок tick у UART */
#define RAD_TO_DEG            57.29578f

/* ---- Хендли ------------------------------------------------------------- */
UART_HandleTypeDef huart2;
I2C_HandleTypeDef  hi2c1;
ADC_HandleTypeDef  hadc1;

/* ---- Прототипи ---------------------------------------------------------- */
static HAL_StatusTypeDef SystemClock_Config(void);
static HAL_StatusTypeDef MX_GPIO_Init(void);
static HAL_StatusTypeDef MX_USART2_UART_Init(void);
static HAL_StatusTypeDef MX_I2C1_Init(void);
static HAL_StatusTypeDef MX_ADC1_Init(void);

static void     report_status(const char *name, HAL_StatusTypeDef st);
static void     i2c_scan(void);
static uint8_t  mpu6050_probe(void);
static int      mpu6050_read_accel(int16_t acc[3]);
static uint16_t adc_read_pot(void);

static const char *const hal_status_str[] = { "OK", "ERROR", "BUSY", "TIMEOUT" };

int main(void)
{
  HAL_Init();
  HAL_StatusTypeDef clk_st = SystemClock_Config();

  /* UART піднімаємо першим — далі все логуємо через printf. */
  HAL_StatusTypeDef gpio_st = MX_GPIO_Init();
  HAL_StatusTypeDef uart_st = MX_USART2_UART_Init();
  /* nosys _fstat() повертає помилку, тож newlib буферизує stdout повністю — вимикаємо буфер. */
  setvbuf(stdout, NULL, _IONBF, 0);

  printf("\r\n==== LR1 bring-up: NUCLEO-C031C6 (STM32C031C6, Cortex-M0+) ====\r\n");
  printf("HAL v%lu.%lu.%lu, SYSCLK = %lu Hz\r\n",
         (HAL_GetHalVersion() >> 24) & 0xFFU, (HAL_GetHalVersion() >> 16) & 0xFFU,
         (HAL_GetHalVersion() >> 8) & 0xFFU, HAL_RCC_GetSysClockFreq());

  report_status("SystemClock 48MHz", clk_st);
  report_status("GPIO (LED, B1)", gpio_st);
  report_status("USART2 115200 8N1", uart_st);
  report_status("I2C1 100 kHz", MX_I2C1_Init());
  report_status("ADC1 IN0 12-bit", MX_ADC1_Init());

  i2c_scan();
  uint8_t mpu_ok = mpu6050_probe();
  HAL_StatusTypeDef lcd_st = lcd1602_init(&hi2c1);
  report_status("LCD1602 @0x27 init", lcd_st);
  if (lcd_st == HAL_OK)
  {
    lcd1602_set_cursor(0, 0);
    lcd1602_print("LR1 bring-up    ");
    lcd1602_set_cursor(0, 1);
    lcd1602_print("NUCLEO-C031C6   ");
  }
  printf("Ready. B1 -> LIVE/HOLD.\r\n\r\n");

  uint32_t tick_n = 0;
  uint32_t now = HAL_GetTick();
  uint32_t next_poll = now, next_display = now + 1000U, next_report = now + REPORT_PERIOD_MS;
  uint8_t  hold = 0, btn_prev = 0;
  uint16_t pot = 0;
  int16_t  acc[3] = { 0, 0, 0 };
  int      pitch = 0, roll = 0;

  while (1)
  {
    now = HAL_GetTick();

    if ((int32_t)(now - next_poll) >= 0)
    {
      next_poll += POLL_PERIOD_MS;
      /* Кнопка B1 (active low): по фронту натискання перемикаємо HOLD. */
      uint8_t btn = (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_RESET);
      if (btn && !btn_prev)
      {
        hold = !hold;
        printf("[B1] %s\r\n", hold ? "HOLD" : "LIVE");
      }
      btn_prev = btn;
    }

    if ((int32_t)(now - next_display) >= 0)
    {
      next_display += DISPLAY_PERIOD_MS;
      if (!hold)
      {
        pot = adc_read_pot();
        if (mpu_ok && mpu6050_read_accel(acc) == 0)
        {
          float ax = acc[0], ay = acc[1], az = acc[2];
          pitch = (int)lroundf(atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG);
          roll  = (int)lroundf(atan2f(ay, az) * RAD_TO_DEG);
        }
      }
      if (lcd_st == HAL_OK)
      {
        char line[17];
        snprintf(line, sizeof line, "P:%+4d R:%+4d%s", pitch, roll, hold ? " H " : "   ");
        lcd1602_set_cursor(0, 0);
        lcd1602_print(line);
        unsigned raw = pot & 0x0FFFU; /* 12-бітний ADC: 0..4095 */
        unsigned pct = (raw * 100U + 2047U) / 4095U;
        snprintf(line, sizeof line, "POT:%4u  %3u%%  ", raw, pct > 100U ? 100U : pct);
        lcd1602_set_cursor(0, 1);
        lcd1602_print(line);
      }
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, hold ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    if ((int32_t)(now - next_report) >= 0)
    {
      next_report += REPORT_PERIOD_MS;
      tick_n++;
      printf("tick %lu | t=%lu ms | pot=%4u | acc=%6d %6d %6d | pitch=%+4d roll=%+4d | %s\r\n",
             tick_n, now, pot, acc[0], acc[1], acc[2], pitch, roll, hold ? "HOLD" : "LIVE");
    }
  }
}

/* ======================================================================== */
/*                        Діагностика / bring-up                            */
/* ======================================================================== */

static void report_status(const char *name, HAL_StatusTypeDef st)
{
  printf("[%s] %s\r\n", st == HAL_OK ? " OK " : "FAIL", name);
  if (st != HAL_OK)
  {
    printf("       -> HAL_%s\r\n", hal_status_str[st & 3U]);
  }
}

/** Перебирає 7-бітні адреси 0x08..0x77 через HAL_I2C_IsDeviceReady(). */
static void i2c_scan(void)
{
  uint32_t found = 0;
  printf("I2C1 scan:");
  for (uint16_t addr = 0x08; addr <= 0x77; addr++)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1, I2C_TIMEOUT_MS) == HAL_OK)
    {
      printf(" 0x%02X", addr);
      found++;
    }
  }
  printf(found ? "  (%lu device(s))\r\n" : " none\r\n", found);
}

/** Читає WHO_AM_I і виводить MPU-6050 зі sleep. Повертає 1, якщо чіп відповів 0x68. */
static uint8_t mpu6050_probe(void)
{
  uint8_t who = 0;
  if (HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR, MPU6050_REG_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,
                       &who, 1, I2C_TIMEOUT_MS) != HAL_OK)
  {
    printf("MPU-6050: no ACK at 0x68\r\n");
    return 0;
  }
  printf("MPU-6050: WHO_AM_I = 0x%02X (%s)\r\n", who,
         who == MPU6050_WHO_AM_I_VAL ? "match" : "unexpected");

  uint8_t wake = 0x00; /* PWR_MGMT_1: SLEEP=0, CLKSEL=internal 8 MHz */
  HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR, MPU6050_REG_PWR_MGMT1, I2C_MEMADD_SIZE_8BIT,
                    &wake, 1, I2C_TIMEOUT_MS);
  return who == MPU6050_WHO_AM_I_VAL;
}

/** Сирі значення акселерометра (±2 g, 16384 LSB/g). 0 — успіх. */
static int mpu6050_read_accel(int16_t acc[3])
{
  uint8_t raw[6];
  if (HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR, MPU6050_REG_ACCEL_X_H, I2C_MEMADD_SIZE_8BIT,
                       raw, sizeof raw, I2C_TIMEOUT_MS) != HAL_OK)
  {
    return -1;
  }
  for (int i = 0; i < 3; i++)
  {
    acc[i] = (int16_t)((raw[2 * i] << 8) | raw[2 * i + 1]);
  }
  return 0;
}

static uint16_t adc_read_pot(void)
{
  uint16_t v = 0;
  if (HAL_ADC_Start(&hadc1) == HAL_OK &&
      HAL_ADC_PollForConversion(&hadc1, 5) == HAL_OK)
  {
    v = (uint16_t)HAL_ADC_GetValue(&hadc1);
  }
  HAL_ADC_Stop(&hadc1);
  return v;
}

/* ======================================================================== */
/*                     Ініціалізація (у стилі CubeMX)                       */
/* ======================================================================== */

/**
  * HSI48 /1 -> SYSCLK 48 MHz, AHB /1, APB /1 (PCLK = 48 MHz).
  * Для 48 MHz потрібна 1 wait state flash (RM0490, табл. "Flash wait states").
  */
static HAL_StatusTypeDef SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = { 0 };
  RCC_ClkInitTypeDef clk = { 0 };

  osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState            = RCC_HSI_ON;
  osc.HSIDiv              = RCC_HSI_DIV1;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    return HAL_ERROR;
  }

  clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
  clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;
  clk.AHBCLKDivider  = RCC_HCLK_DIV1;
  clk.APB1CLKDivider = RCC_APB1_DIV1;
  return HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1);
}

static HAL_StatusTypeDef MX_GPIO_Init(void)
{
  GPIO_InitTypeDef g = { 0 };

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  g.Pin   = LED_Pin;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &g);

  /* На Nucleo B1 має зовнішній pull-up; внутрішній — для Wokwi/запасу. */
  g.Pin   = BTN_Pin;
  g.Mode  = GPIO_MODE_INPUT;
  g.Pull  = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_GPIO_Port, &g);

  return HAL_OK; /* HAL_GPIO_Init() нічого не повертає */
}

static HAL_StatusTypeDef MX_USART2_UART_Init(void)
{
  huart2.Instance                    = USART2;
  huart2.Init.BaudRate               = 115200;
  huart2.Init.WordLength             = UART_WORDLENGTH_8B;
  huart2.Init.StopBits               = UART_STOPBITS_1;
  huart2.Init.Parity                 = UART_PARITY_NONE;
  huart2.Init.Mode                   = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  return HAL_UART_Init(&huart2);
}

/**
  * Timing 0xB0420F13 для I2CCLK = 48 MHz, Standard mode 100 kHz:
  * PRESC=11 (tPRESC=250 нс), SCLL=0x13 (5 мкс), SCLH=0x0F (4 мкс),
  * SDADEL=2, SCLDEL=4 — відповідає RM0490 "I2C timing settings" (масштабовано з 8 MHz).
  */
static HAL_StatusTypeDef MX_I2C1_Init(void)
{
  hi2c1.Instance              = I2C1;
  hi2c1.Init.Timing           = 0xB0420F13;
  hi2c1.Init.OwnAddress1      = 0;
  hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2      = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0);
}

static HAL_StatusTypeDef MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef ch = { 0 };

  hadc1.Instance                   = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV2; /* 24 MHz <= 35 MHz max */
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait      = DISABLE;
  hadc1.Init.LowPowerAutoPowerOff  = DISABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.NbrOfConversion       = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun               = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTimeCommon1   = ADC_SAMPLETIME_39CYCLES_5;
  hadc1.Init.OversamplingMode      = DISABLE;
  hadc1.Init.TriggerFrequencyMode  = ADC_TRIGGER_FREQ_HIGH;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    return HAL_ERROR;
  }

  ch.Channel      = POT_ADC_CHANNEL;
  ch.Rank         = ADC_REGULAR_RANK_1;
  ch.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK)
  {
    return HAL_ERROR;
  }

  /* Калібрування не критичне для bring-up: якщо не вдалось — лише попередження. */
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    printf("[WARN] ADC1 calibration failed, using uncalibrated values\r\n");
  }
  return HAL_OK;
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  printf("assert failed: %s:%lu\r\n", file, line);
}
#endif
