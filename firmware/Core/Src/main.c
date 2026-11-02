/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   Точка входу. Структура файлу — як у STM32CubeMX: ініціалізація
  *          периферії у функціях MX_*_Init(), власний код лише між маркерами
  *          USER CODE BEGIN/END, щоб повторна генерація з .ioc його не стерла.
  *
  *          main.c — composition root: збирає app_hw_t з об'єктів Platform/,
  *          драйверів Devices/ і механізмів ОС Rtos/, запускає планувальник FreeRTOS.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

/* USER CODE BEGIN Includes */
#include "app.h"
#include "platform.h"
#include "rtos_app.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
static mpu6050_t mpu;
static w25q64_t flash;
static servo_t servo_pan;
static servo_t servo_tilt;
static app_t app;
/* Час для драйверів: мілісекунди HAL (TIM14); затримка — vTaskDelay після старту планувальника,
 * до нього (самоперевірка в app_init) — HAL_Delay */
static void os_delay_ms(uint32_t ms)
{
  if (!rtos_delay_ms(ms))
  {
    platform_time.delay_ms(ms);
  }
}
static const dev_time_t os_time = { platform_millis, os_delay_ms };
static void led_set(app_led_t led, bool on);
static bool led_get(app_led_t led);
/* Статичний ініціалізатор, а не присвоєння в main(): менше коду (ЛР4, флеш під ядро ОС) */
static app_hw_t hw = {
    .i2c = &platform_i2c1,
    .spi = &platform_spi1_flash,
    .time = &os_time,
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
    .cycles = platform_cycles,
    .micros = platform_micros,
    .cal_loop = platform_cal_loop,
    .probe = platform_probe,
    .btn_take = platform_btn_take,
    .rx_eol_take = platform_rx_eol_take,
    .rx_dropped = platform_rx_dropped,
    .stack_used = platform_stack_used,
    .stack_size = platform_stack_size,
    .reset = platform_reset,
    .lock = rtos_lock,
    .unlock = rtos_unlock,
    .crit = rtos_crit,
    .log_post = rtos_log_post,
    .mode_post = rtos_mode_post,
    .rec_post = rtos_rec_post,
    .long_post = rtos_long_post,
    .os_cmd = rtos_os_cmd,
    .board_name = BOARD_NAME,
    .fw_version = FW_VERSION,
    .fw_git = FW_GIT_HASH,
};
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
  /* USER CODE BEGIN 1 */
  platform_stack_paint();
  /* USER CODE END 1 */
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM3_Init();

  /* USER CODE BEGIN 2 */
  servo_init(&servo_pan, &platform_pwm_pan, SERVO_DEFAULT_MIN_US, SERVO_DEFAULT_MAX_US);
  servo_init(&servo_tilt, &platform_pwm_tilt, SERVO_DEFAULT_MIN_US, SERVO_DEFAULT_MAX_US);

  hw.sysclk_hz = platform_sysclk_hz(); /* після SystemClock_Config */
  hw.tick_us = rtos_tick_us();
  platform_con_start_rx();
  app_init(&app, &hw); /* до старту ОС: банер, самоперевірка, сканування траєкторії */
  rtos_start(&app, &hw);
  /* USER CODE END 2 */

  while (1)
  {
  }
}

/*
 * Ініціалізація тактування, UART, SPI і TIM3 — на регістрах (ЛР4). Значення ті самі, що давали
 * HAL_RCC_*Config, HAL_UART_Init, HAL_SPI_Init і HAL_TIM_PWM_*. Обмін по SPI і I2C — теж на регістрах
 * (Platform/platform.c): сигнали на шинах ті самі, змінилась лише кількість інструкцій між байтами. Звільнило флеш під ядро FreeRTOS (docs/metrics/memory.md).
 */

/** HSI48 /1 -> SYSCLK 48 МГц, AHB /1, APB /1. 1 wait state (RM0490 Table 14 p.59). */
static void SystemClock_Config(void)
{
  FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_0; /* спершу затримка флеш */
  while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_0)
  {
  }
  RCC->CR &= ~RCC_CR_HSIDIV; /* HSIDIV = 1; після скидання 4 -> 12 МГц */
  RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE); /* SYSCLK = HSISYS, AHB /1, APB /1 */
  SystemCoreClockUpdate();
  if (HAL_InitTick(TICK_INT_PRIORITY) != HAL_OK)
  {
    Error_Handler();
  }
}

/** 8N1, oversampling 16, FIFO вимкнено — як HAL_UART_Init з полями CubeMX. */
static void uart_init_common(UART_HandleTypeDef *h, USART_TypeDef *inst, uint32_t baud)
{
  h->Instance = inst;
  HAL_UART_MspInit(h); /* тактування і піни */
  inst->CR1 = 0;
  inst->CR2 = 0;
  inst->CR3 = 0;
  inst->PRESC = 0;
  inst->BRR = (BOARD_PCLK_HZ + baud / 2u) / baud; /* 48e6/115200 -> 417, як UART_DIV_SAMPLING16 */
  inst->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
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
  platform_i2c_init(); /* Fast-mode 400 кГц, TIMINGR з RM0490 Table 115 (Platform/board.h) */
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
  HAL_SPI_MspInit(&hspi1);
  /* Ті самі біти, що пише HAL_SPI_Init (RM0490 §26.7): майстер, NSS програмний, 8 біт, FRXTH для 8 біт */
  SPI1->CR1 = SPI_CR1_MSTR | SPI_PRESCALER | SPI_CR1_SSM | SPI_CR1_SSI;
  SPI1->CR2 = SPI_DATASIZE_8BIT | SPI_CR2_FRXTH;
  /* обмін — на регістрах, Platform/platform.c spi_transfer() */
}

/** TIM3: 1 тік = 1 мкс, період 20 мс, PWM1 на CH1/CH2 із попереднім завантаженням CCR і ARR. */
static void MX_TIM3_Init(void)
{
  htim3.Instance = TIM3;
  __HAL_RCC_TIM3_CLK_ENABLE();
  TIM3->PSC = SERVO_TIM_PSC;
  TIM3->ARR = SERVO_TIM_ARR;
  TIM3->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE | (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
  TIM3->CCR1 = 1472; /* 90° для діапазону 544..2400 мкс */
  TIM3->CCR2 = 1472;
  TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
  TIM3->CR1 = TIM_CR1_ARPE;
  TIM3->EGR = TIM_EGR_UG;
  HAL_TIM_MspPostInit(&htim3); /* піни ШІМ — після конфігурації каналів, щоб серво не смикались */
  TIM3->CR1 |= TIM_CR1_CEN;
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
  board_gpio_init(FLASH_CS_PORT, &g);

  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Pin = LED_LIVE_PIN;
  board_gpio_init(LED_LIVE_PORT, &g);
  g.Pin = LED_REC_PIN;
  board_gpio_init(LED_REC_PORT, &g);

  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Pin = MARK_PIN;
  board_gpio_init(MARK_PORT, &g);

  g.Pin = PROBE0_PIN | PROBE1_PIN | PROBE2_PIN;
  board_gpio_init(PROBE0_PORT, &g); /* PA0, PA1, PA4 — службові GPIO, спочатку низький рівень */
  g.Pin = PROBE3_PIN;
  board_gpio_init(PROBE3_PORT, &g);

  g.Mode = GPIO_MODE_IT_FALLING; /* B1 натиснута = низький рівень */
  g.Pull = GPIO_PULLUP;          /* як у BSP ST для B1 (stm32c0xx_nucleo.c) */
  g.Pin = BTN_PIN;
  board_gpio_init(BTN_PORT, &g);
  HAL_NVIC_SetPriority(BTN_EXTI_IRQn, BTN_EXTI_PRIO, 0);
  HAL_NVIC_EnableIRQ(BTN_EXTI_IRQn);
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
