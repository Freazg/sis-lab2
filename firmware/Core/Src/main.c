/**
  ******************************************************************************
  * @file    main.c
  * @brief   ЛР1 — bring-up платформи NUCLEO-C031C6 (Wokwi) на STM32 HAL.
  *
  *  1. Ініціалізує тактування (HSI48 -> SYSCLK 48 MHz) і периферію:
  *     USART2 (VCP), I2C1, ADC1, TIM3 (PWM 50 Hz), SPI1, GPIO.
  *  2. Друкує таблицю статусів HAL_OK для кожної ініціалізації.
  *  3. Сканує I2C-шину, читає WHO_AM_I з MPU-6050 і JEDEC ID з SPI-флеш.
  *  4. У циклі: потенціометр -> серво pan, нахил MPU-6050 -> серво tilt,
  *     кнопка B1 -> режим HOLD, щосекунди рядок "tick N ..." у UART.
  ******************************************************************************
  */
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ---- Константи ---------------------------------------------------------- */
#define MPU6050_ADDR          (0x68U << 1)  /* HAL очікує 8-бітну адресу */
#define MPU6050_REG_PWR_MGMT1 0x6BU
#define MPU6050_REG_ACCEL_X_H 0x3BU
#define MPU6050_REG_WHO_AM_I  0x75U
#define MPU6050_WHO_AM_I_VAL  0x68U

#define W25Q_CMD_JEDEC_ID     0x9FU

#define I2C_TIMEOUT_MS        10U
#define SERVO_MIN_US          544U   /* 0°   */
#define SERVO_MAX_US          2400U  /* 180° */
#define CONTROL_PERIOD_MS     20U    /* 50 Hz, як і PWM серво */
#define REPORT_PERIOD_MS      1000U

/* ---- Хендли ------------------------------------------------------------- */
UART_HandleTypeDef huart2;
I2C_HandleTypeDef  hi2c1;
ADC_HandleTypeDef  hadc1;
TIM_HandleTypeDef  htim3;
SPI_HandleTypeDef  hspi1;

/* ---- Прототипи ---------------------------------------------------------- */
static HAL_StatusTypeDef SystemClock_Config(void);
static HAL_StatusTypeDef MX_GPIO_Init(void);
static HAL_StatusTypeDef MX_USART2_UART_Init(void);
static HAL_StatusTypeDef MX_I2C1_Init(void);
static HAL_StatusTypeDef MX_ADC1_Init(void);
static HAL_StatusTypeDef MX_TIM3_Init(void);
static HAL_StatusTypeDef MX_SPI1_Init(void);

static void     report_status(const char *name, HAL_StatusTypeDef st);
static void     i2c_scan(void);
static uint8_t  mpu6050_probe(void);
static void     w25q_probe(void);
static int      mpu6050_read_accel(int16_t acc[3]);
static uint16_t adc_read_pot(void);
static void     servo_write_deg(uint32_t channel, uint32_t deg);

static const char *const hal_status_str[] = { "OK", "ERROR", "BUSY", "TIMEOUT" };

int main(void)
{
  HAL_Init();
  HAL_StatusTypeDef clk_st = SystemClock_Config();

  /* UART піднімаємо першим — далі все логуємо через printf. */
  HAL_StatusTypeDef gpio_st = MX_GPIO_Init();
  HAL_StatusTypeDef uart_st = MX_USART2_UART_Init();
  /* nosys _isatty() повертає 0, тож newlib буферизує stdout повністю — вимикаємо буфер. */
  setvbuf(stdout, NULL, _IONBF, 0);

  printf("\r\n==== LR1 bring-up: NUCLEO-C031C6 (STM32C031C6, Cortex-M0+) ====\r\n");
  printf("HAL v%lu.%lu.%lu, SYSCLK = %lu Hz\r\n",
         (HAL_GetHalVersion() >> 24) & 0xFFU, (HAL_GetHalVersion() >> 16) & 0xFFU,
         (HAL_GetHalVersion() >> 8) & 0xFFU, HAL_RCC_GetSysClockFreq());

  report_status("SystemClock 48MHz", clk_st);
  report_status("GPIO (LED, B1, CS)", gpio_st);
  report_status("USART2 115200 8N1", uart_st);
  report_status("I2C1 100 kHz", MX_I2C1_Init());
  report_status("ADC1 IN0 12-bit", MX_ADC1_Init());
  report_status("TIM3 PWM 50 Hz", MX_TIM3_Init());
  report_status("SPI1 master mode0", MX_SPI1_Init());

  report_status("TIM3 CH1 start", HAL_TIM_PWM_Start(&htim3, SERVO_PAN_CHANNEL));
  report_status("TIM3 CH2 start", HAL_TIM_PWM_Start(&htim3, SERVO_TILT_CHANNEL));

  i2c_scan();
  uint8_t mpu_ok = mpu6050_probe();
  w25q_probe();
  printf("Ready. Pot -> pan, MPU pitch -> tilt, B1 -> HOLD.\r\n\r\n");

  uint32_t tick_n = 0;
  uint32_t next_ctrl = HAL_GetTick();
  uint32_t next_report = next_ctrl + REPORT_PERIOD_MS;
  uint8_t  hold = 0, btn_prev = 0;
  uint16_t pot = 0;
  uint32_t pan = 90, tilt = 90;
  int16_t  acc[3] = { 0, 0, 0 };

  while (1)
  {
    uint32_t now = HAL_GetTick();

    if ((int32_t)(now - next_ctrl) >= 0)
    {
      next_ctrl += CONTROL_PERIOD_MS;

      /* Кнопка B1 (active low): по фронту натискання перемикаємо HOLD. */
      uint8_t btn = (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_RESET);
      if (btn && !btn_prev)
      {
        hold = !hold;
        printf("[B1] HOLD %s\r\n", hold ? "ON" : "OFF");
      }
      btn_prev = btn;

      pot = adc_read_pot();
      if (mpu_ok && mpu6050_read_accel(acc) == 0)
      {
        /* pitch = atan2(ax, az): -90..+90° -> 0..180° на серво. */
        float pitch = atan2f((float)acc[0], (float)acc[2]) * 57.29578f;
        if (pitch < -90.0f) pitch = -90.0f;
        if (pitch >  90.0f) pitch =  90.0f;
        if (!hold) tilt = (uint32_t)(pitch + 90.0f + 0.5f);
      }
      if (!hold) pan = ((uint32_t)pot * 180U + 2047U) / 4095U;

      servo_write_deg(SERVO_PAN_CHANNEL, pan);
      servo_write_deg(SERVO_TILT_CHANNEL, tilt);
      if (hold) HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET); /* HOLD: горить, LIVE: блимає */
    }

    if ((int32_t)(now - next_report) >= 0)
    {
      next_report += REPORT_PERIOD_MS;
      tick_n++;
      printf("tick %lu | t=%lu ms | pot=%4u | acc=%6d %6d %6d | pan=%3lu tilt=%3lu | %s\r\n",
             tick_n, now, pot, acc[0], acc[1], acc[2], pan, tilt, hold ? "HOLD" : "LIVE");
      if (!hold) HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
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

/** Читає JEDEC ID (0x9F). На W25Q64 очікуємо EF 40 17. */
static void w25q_probe(void)
{
  uint8_t tx[4] = { W25Q_CMD_JEDEC_ID, 0, 0, 0 };
  uint8_t rx[4] = { 0 };

  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof tx, 10);
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);

  if (st != HAL_OK)
  {
    printf("W25Q64: SPI transfer HAL_%s\r\n", hal_status_str[st & 3U]);
    return;
  }
  int absent = (rx[1] == 0xFF && rx[2] == 0xFF && rx[3] == 0xFF) ||
               (rx[1] == 0x00 && rx[2] == 0x00 && rx[3] == 0x00);
  printf("W25Q64: JEDEC ID = %02X %02X %02X (%s)\r\n", rx[1], rx[2], rx[3],
         absent ? "not connected" : (rx[1] == 0xEF ? "Winbond" : "unknown vendor"));
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

/** TIM3 тикає з 1 MHz, тож CCR = ширина імпульсу в мкс. */
static void servo_write_deg(uint32_t channel, uint32_t deg)
{
  if (deg > 180U) deg = 180U;
  uint32_t us = SERVO_MIN_US + (deg * (SERVO_MAX_US - SERVO_MIN_US) + 90U) / 180U;
  __HAL_TIM_SET_COMPARE(&htim3, channel, us);
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
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET); /* CS# неактивний */

  g.Pin   = LED_Pin;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &g);

  g.Pin   = FLASH_CS_Pin;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(FLASH_CS_GPIO_Port, &g);

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

/** TIM3: 48 MHz / 48 = 1 MHz, ARR = 20000 -> 50 Hz; CCR = мкс імпульсу. */
static HAL_StatusTypeDef MX_TIM3_Init(void)
{
  TIM_MasterConfigTypeDef master = { 0 };
  TIM_OC_InitTypeDef oc = { 0 };

  htim3.Instance               = TIM3;
  htim3.Init.Prescaler         = (HAL_RCC_GetPCLK1Freq() / 1000000U) - 1U;
  htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim3.Init.Period            = 20000U - 1U;
  htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    return HAL_ERROR;
  }

  master.MasterOutputTrigger = TIM_TRGO_RESET;
  master.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &master) != HAL_OK)
  {
    return HAL_ERROR;
  }

  oc.OCMode     = TIM_OCMODE_PWM1;
  oc.Pulse      = (SERVO_MIN_US + SERVO_MAX_US) / 2U; /* 90° */
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc, SERVO_PAN_CHANNEL) != HAL_OK ||
      HAL_TIM_PWM_ConfigChannel(&htim3, &oc, SERVO_TILT_CHANNEL) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_TIM_MspPostInit(&htim3);
  return HAL_OK;
}

/** SPI1 master, mode 0 (CPOL=0, CPHA=0), 48 MHz / 16 = 3 MHz — W25Q64 тягне до 104 MHz. */
static HAL_StatusTypeDef MX_SPI1_Init(void)
{
  hspi1.Instance               = SPI1;
  hspi1.Init.Mode              = SPI_MODE_MASTER;
  hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
  hspi1.Init.NSS               = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial     = 7;
  hspi1.Init.CRCLength         = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
  return HAL_SPI_Init(&hspi1);
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
