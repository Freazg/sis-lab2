#include "platform.h"

#include "board.h"

/* ---- Переклад HAL -> dev_status_t -------------------------------------- */
static dev_status_t from_hal(HAL_StatusTypeDef st, uint32_t hal_error)
{
  if (st == HAL_OK) {
    return DEV_OK;
  }
  if (hal_error & HAL_I2C_ERROR_AF) {
    return DEV_ERR_NACK;
  }
  if (st == HAL_TIMEOUT || (hal_error & HAL_I2C_ERROR_TIMEOUT)) {
    return DEV_ERR_TIMEOUT;
  }
  return DEV_ERR_BUS;
}

/* ---- I2C1 ---------------------------------------------------------------- */
#if BOARD_I2C_HAL_TRANSMIT
static dev_status_t i2c_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
  I2C_HandleTypeDef *h = ctx;
  HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(h, (uint16_t)(addr7 << 1), (uint8_t *)data,
                                                 (uint16_t)len, I2C_TIMEOUT_MS);
  return from_hal(st, HAL_I2C_GetError(h));
}
#else
/*
 * Запис на рівні регістрів I2C (RM0490 §25.4.9 «Controller transmitter»): NBYTES, AUTOEND, START,
 * далі по одному байту в TXDR на кожен TXIS, завершення — STOPF.
 * HAL_I2C_Master_Transmit (stm32c0xx_hal_i2c.c:1166) завантажує перший байт у TXDR ще до START;
 * модель Wokwi після цього передає лише 1 байт і STOP (аномалія A3, docs/captures/analysis.md).
 */
static int i2c_wait(I2C_TypeDef *i2c, uint32_t mask, uint32_t t0)
{
  while ((i2c->ISR & (mask | I2C_ISR_NACKF)) == 0u) {
    if (HAL_GetTick() - t0 > I2C_TIMEOUT_MS) {
      return -1;
    }
  }
  return (i2c->ISR & I2C_ISR_NACKF) ? 1 : 0;
}

static dev_status_t i2c_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
  I2C_HandleTypeDef *h = ctx;
  I2C_TypeDef *i2c = h->Instance;
  uint32_t t0 = HAL_GetTick();
  if (len == 0u || len > 255u) {
    return DEV_ERR_PARAM;
  }
  while (i2c->ISR & I2C_ISR_BUSY) {
    if (HAL_GetTick() - t0 > I2C_TIMEOUT_MS) {
      return DEV_ERR_BUS;
    }
  }
  /* Скидання прапорців контролера (PE=0 -> 1, RM0490 §25.4.5 «software reset»):
   * у Wokwi після попереднього читання TXIS лишається встановленим, і без цього
   * перший байт потрапляв у TXDR ще до адресної фази. На реальному кристалі скидання
   * лише очищає ISR і нічого не змінює на шині (шина вільна — BUSY перевірено вище). */
  i2c->CR1 &= ~I2C_CR1_PE;
  __DSB();
  i2c->CR1 |= I2C_CR1_PE;
  i2c->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
  i2c->CR2 = ((uint32_t)addr7 << 1) | ((uint32_t)len << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;
  int r = 0;
  for (size_t i = 0; i < len && r == 0; i++) {
    r = i2c_wait(i2c, I2C_ISR_TXIS, t0);
    if (r == 0) {
      i2c->TXDR = data[i];
    }
  }
  if (r == 0) {
    r = i2c_wait(i2c, I2C_ISR_STOPF, t0);
  }
  if (r == 1) {
    (void)i2c_wait(i2c, I2C_ISR_STOPF, t0); /* після NACK апаратура сама генерує STOP (AUTOEND) */
  }
  i2c->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
  i2c->CR2 = 0;
  return r == 0 ? DEV_OK : (r == 1 ? DEV_ERR_NACK : DEV_ERR_TIMEOUT);
}
#endif

static dev_status_t i2c_write_read(void *ctx, uint8_t addr7, const uint8_t *wdata, size_t wlen,
                                   uint8_t *rdata, size_t rlen)
{
  I2C_HandleTypeDef *h = ctx;
  HAL_StatusTypeDef st;
#if BOARD_I2C_REPEATED_START
  if (wlen == 1) {
    st = HAL_I2C_Mem_Read(h, (uint16_t)(addr7 << 1), wdata[0], I2C_MEMADD_SIZE_8BIT, rdata, (uint16_t)rlen,
                          I2C_TIMEOUT_MS);
    return from_hal(st, HAL_I2C_GetError(h));
  }
#endif
  /* START addr+W reg STOP, START addr+R data... STOP. MPU-6050 зберігає вказівник регістра
   * між транзакціями, тому результат той самий, що й з repeated START. */
  dev_status_t w = i2c_write(ctx, addr7, wdata, wlen);
  if (w != DEV_OK) {
    return w;
  }
  st = HAL_I2C_Master_Receive(h, (uint16_t)(addr7 << 1), rdata, (uint16_t)rlen, I2C_TIMEOUT_MS);
  return from_hal(st, HAL_I2C_GetError(h));
}

static dev_status_t i2c_probe(void *ctx, uint8_t addr7)
{
  I2C_HandleTypeDef *h = ctx;
  HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(h, (uint16_t)(addr7 << 1), 1, I2C_TIMEOUT_MS);
  return from_hal(st, HAL_I2C_GetError(h));
}

const i2c_bus_t platform_i2c1 = {
  .write = i2c_write,
  .write_read = i2c_write_read,
  .probe = i2c_probe,
  .ctx = &hi2c1,
};

dev_status_t platform_i2c_mem_read_restart(uint8_t addr7, uint8_t reg, uint8_t *val)
{
  HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(addr7 << 1), reg, I2C_MEMADD_SIZE_8BIT, val, 1,
                                          I2C_TIMEOUT_MS);
  return from_hal(st, HAL_I2C_GetError(&hi2c1));
}

void platform_i2c_recover(void)
{
  /* RM0490 §25.4: при зависанні контролера — скинути PE; HAL_I2C_DeInit/Init робить це
   * і заново налаштовує піни через MSP. */
  HAL_I2C_DeInit(&hi2c1);
  HAL_I2C_Init(&hi2c1);
  HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);
  HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0);
}

/* ---- SPI1 + CS# PB0 ------------------------------------------------------ */
static void spi_select(void *ctx, bool active)
{
  (void)ctx;
  HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, active ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static dev_status_t spi_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
  SPI_HandleTypeDef *h = ctx;
  HAL_StatusTypeDef st;
  if (len == 0) {
    return DEV_OK;
  }
  if (tx != NULL && rx != NULL) {
    st = HAL_SPI_TransmitReceive(h, (uint8_t *)tx, rx, (uint16_t)len, SPI_TIMEOUT_MS);
  } else if (tx != NULL) {
    st = HAL_SPI_Transmit(h, (uint8_t *)tx, (uint16_t)len, SPI_TIMEOUT_MS);
  } else {
    st = HAL_SPI_Receive(h, rx, (uint16_t)len, SPI_TIMEOUT_MS); /* MOSI = 0xFF/0x00 dummy */
  }
  return st == HAL_OK ? DEV_OK : (st == HAL_TIMEOUT ? DEV_ERR_TIMEOUT : DEV_ERR_BUS);
}

const spi_bus_t platform_spi1_flash = {
  .select = spi_select,
  .transfer = spi_transfer,
  .ctx = &hspi1,
};

/* ---- TIM3 PWM ------------------------------------------------------------ */
typedef struct {
  TIM_HandleTypeDef *htim;
  uint32_t channel;
} pwm_ctx_t;

static const pwm_ctx_t pwm_pan_ctx = { &htim3, SERVO_PAN_CH };
static const pwm_ctx_t pwm_tilt_ctx = { &htim3, SERVO_TILT_CH };

static void pwm_set(void *ctx, uint32_t us)
{
  const pwm_ctx_t *p = ctx;
  if (us > SERVO_TIM_ARR) {
    us = SERVO_TIM_ARR;
  }
  __HAL_TIM_SET_COMPARE(p->htim, p->channel, us); /* 1 тік = 1 мкс */
}

static uint32_t pwm_get(void *ctx)
{
  const pwm_ctx_t *p = ctx;
  return __HAL_TIM_GET_COMPARE(p->htim, p->channel);
}

static uint32_t pwm_period(void *ctx)
{
  const pwm_ctx_t *p = ctx;
  /* Читаємо фактичні регістри, а не константи: період = (ARR+1)·(PSC+1)/f_TIM */
  uint32_t psc = p->htim->Instance->PSC;
  uint32_t arr = p->htim->Instance->ARR;
  return (uint32_t)(((uint64_t)(arr + 1u) * (psc + 1u) * 1000000u) / BOARD_PCLK_HZ);
}

const pwm_out_t platform_pwm_pan = { pwm_set, pwm_get, pwm_period, (void *)&pwm_pan_ctx };
const pwm_out_t platform_pwm_tilt = { pwm_set, pwm_get, pwm_period, (void *)&pwm_tilt_ctx };

/* ---- Час ------------------------------------------------------------------ */
static uint32_t millis(void) { return HAL_GetTick(); }
static void delay_ms(uint32_t ms) { HAL_Delay(ms); }

const dev_time_t platform_time = { millis, delay_ms };

/* ---- GPIO ------------------------------------------------------------------ */
static GPIO_TypeDef *const led_port[2] = { LED_LIVE_PORT, LED_REC_PORT };
static const uint16_t led_pin[2] = { LED_LIVE_PIN, LED_REC_PIN };

void platform_led_set(int idx, bool on)
{
  HAL_GPIO_WritePin(led_port[idx], led_pin[idx], on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool platform_led_get(int idx)
{
  return HAL_GPIO_ReadPin(led_port[idx], led_pin[idx]) == GPIO_PIN_SET; /* IDR, а не ODR */
}

bool platform_button_pressed(void)
{
  GPIO_PinState s = HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN);
  return BTN_ACTIVE_LOW ? (s == GPIO_PIN_RESET) : (s == GPIO_PIN_SET);
}

void platform_mark(bool level)
{
  HAL_GPIO_WritePin(MARK_PORT, MARK_PIN, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ---- UART ------------------------------------------------------------------ */
void platform_con_write(const char *s, size_t len)
{
  HAL_UART_Transmit(&huart2, (const uint8_t *)s, (uint16_t)len, 100);
}

int platform_con_getc(void)
{
  if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE)) {
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF);
  }
  if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE)) {
    return (int)(huart2.Instance->RDR & 0xFFu);
  }
  return -1;
}

dev_status_t platform_aux_write(const char *s, size_t len)
{
  HAL_StatusTypeDef st = HAL_UART_Transmit(&huart1, (const uint8_t *)s, (uint16_t)len, 100);
  return st == HAL_OK ? DEV_OK : (st == HAL_TIMEOUT ? DEV_ERR_TIMEOUT : DEV_ERR_BUS);
}

uint32_t platform_sysclk_hz(void)
{
  return HAL_RCC_GetSysClockFreq();
}
