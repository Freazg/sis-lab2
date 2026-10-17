/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   Точка входу. Структура файлу — як у STM32CubeMX: ініціалізація
  *          периферії у функціях MX_*_Init(), власний код лише між маркерами
  *          USER CODE BEGIN/END, щоб повторна генерація з .ioc його не стерла.
  *
  *          main.c — composition root: збирає app_hw_t з об'єктів Platform/
  *          і драйверів Devices/ і передає керування App/.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

/* USER CODE BEGIN Includes */
#include "app.h"
#include "platform.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
static mpu6050_t mpu;
static w25q64_t flash;
static servo_t servo_pan;
static servo_t servo_tilt;
static app_hw_t hw;
static app_t app;
/* USER CODE END PV */

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM3_Init(void);

/* USER CODE BEGIN 0 */
static void led_set(app_led_t led, bool on) { platform_led_set((int)led, on); }
static bool led_get(app_led_t led) { return platform_led_get((int)led); }
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM3_Init();

  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim3, SERVO_PAN_CH);
  HAL_TIM_PWM_Start(&htim3, SERVO_TILT_CH);

  servo_init(&servo_pan, &platform_pwm_pan, SERVO_DEFAULT_MIN_US, SERVO_DEFAULT_MAX_US);
  servo_init(&servo_tilt, &platform_pwm_tilt, SERVO_DEFAULT_MIN_US, SERVO_DEFAULT_MAX_US);

  hw = (app_hw_t){
    .i2c = &platform_i2c1,
    .spi = &platform_spi1_flash,
    .time = &platform_time,
    .mpu = &mpu,
    .flash = &flash,
    .servo_pan = &servo_pan,
    .servo_tilt = &servo_tilt,
    .led_set = led_set,
    .led_get = led_get,
    .button_pressed = platform_button_pressed,
    .mark = platform_mark,
    .con_write = platform_con_write,
    .con_getc = platform_con_getc,
    .aux_write = platform_aux_write,
    .i2c_mem_read_restart = platform_i2c_mem_read_restart,
    .i2c_recover = platform_i2c_recover,
    .sysclk_hz = platform_sysclk_hz(),
    .board_name = BOARD_NAME,
    .fw_version = FW_VERSION,
  };
  app_init(&app, &hw);
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    app_poll(&app);
    /* USER CODE END 3 */
  }
}

/**
  * HSI48 /1 -> SYSCLK 48 МГц, AHB /1, APB /1. 1 wait state (RM0490 Table 14 p.59).
  */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = { 0 };
  RCC_ClkInitTypeDef clk = { 0 };

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState = RCC_HSI_ON;
  osc.HSIDiv = RCC_HSI_DIV1;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    Error_Handler();
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
  clk.AHBCLKDivider = RCC_HCLK_DIV1;
  clk.APB1CLKDivider = RCC_APB1_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void uart_init_common(UART_HandleTypeDef *h, USART_TypeDef *inst, uint32_t baud)
{
  h->Instance = inst;
  h->Init.BaudRate = baud;
  h->Init.WordLength = UART_WORDLENGTH_8B;
  h->Init.StopBits = UART_STOPBITS_1;
  h->Init.Parity = UART_PARITY_NONE;
  h->Init.Mode = UART_MODE_TX_RX;
  h->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  h->Init.OverSampling = UART_OVERSAMPLING_16;
  h->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  h->Init.ClockPrescaler = UART_PRESCALER_DIV1;
  h->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(h) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART2_UART_Init(void)
{
  uart_init_common(&huart2, USART2, CON_BAUD);
}

static void MX_USART1_UART_Init(void)
{
  uart_init_common(&huart1, USART1, AUX_BAUD);
}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = I2C_TIMINGR_400K;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;   /* mode 0: CPOL=0 */
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;       /*         CPHA=0 */
  hspi1.Init.NSS = SPI_NSS_SOFT;               /* CS# — GPIO PB0 */
  hspi1.Init.BaudRatePrescaler = SPI_PRESCALER;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_TIM3_Init(void)
{
  TIM_MasterConfigTypeDef master = { 0 };
  TIM_OC_InitTypeDef oc = { 0 };

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = SERVO_TIM_PSC;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = SERVO_TIM_ARR;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  master.MasterOutputTrigger = TIM_TRGO_RESET;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &master) != HAL_OK)
  {
    Error_Handler();
  }
  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = 1472; /* 90° для діапазону 544..2400 мкс */
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc, SERVO_PAN_CH) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc, SERVO_TILT_CH) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_TIM_MspPostInit(&htim3);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef g = { 0 };

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET); /* CS# неактивний до першого обміну */
  HAL_GPIO_WritePin(LED_LIVE_PORT, LED_LIVE_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_REC_PORT, LED_REC_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MARK_PORT, MARK_PIN, GPIO_PIN_RESET);

  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Pin = FLASH_CS_PIN;
  HAL_GPIO_Init(FLASH_CS_PORT, &g);

  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Pin = LED_LIVE_PIN;
  HAL_GPIO_Init(LED_LIVE_PORT, &g);
  g.Pin = LED_REC_PIN;
  HAL_GPIO_Init(LED_REC_PORT, &g);

  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Pin = MARK_PIN;
  HAL_GPIO_Init(MARK_PORT, &g);

  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP; /* як у BSP ST для B1 (stm32c0xx_nucleo.c) */
  g.Pin = BTN_PIN;
  HAL_GPIO_Init(BTN_PORT, &g);
}

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
