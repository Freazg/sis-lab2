#include "platform.h"

#include "board.h"

/* ---- I2C1 ---------------------------------------------------------------- */
/*
 * Обмін на регістрах I2C (RM0490 §25.4.9 «Controller transmitter», §25.4.10 «Controller receiver»).
 * Запис був регістровим ще з ЛР2: HAL_I2C_Master_Transmit (stm32c0xx_hal_i2c.c:1166) завантажує
 * перший байт у TXDR ще до START, і модель Wokwi після цього передає лише 1 байт і STOP (аномалія A3,
 * docs/captures/analysis.md). У ЛР4 на регістри переведено й читання, перевірку адреси та ініціалізацію:
 * HAL I2C займав 2 КБ флеш, потрібних ядру FreeRTOS. Кадри на шині ті самі (тест порівняння із записом).
 */
/** Ті самі записи, що HAL_I2C_Init + ConfigAnalogFilter/ConfigDigitalFilter для полів MX_I2C1_Init (ЛР2). */
static void i2c_hw_init(void)
{
  I2C1->CR1 = 0;
  I2C1->TIMINGR = I2C_TIMINGR_400K;
  I2C1->OAR1 = I2C_OAR1_OA1EN; /* власна адреса 0, 7 біт: модель Wokwi без цього ставила ADDR */
  I2C1->CR2 = I2C_CR2_AUTOEND | I2C_CR2_NACK;
  I2C1->OAR2 = 0;
  I2C1->CR1 = I2C_CR1_PE; /* аналоговий фільтр увімкнено (ANFOFF=0), цифровий вимкнено (DNF=0) */
}

void platform_i2c_init(void)
{
  hi2c1.Instance = I2C1;
  HAL_I2C_MspInit(&hi2c1); /* піни AF6 open-drain і тактування, Core/Src/stm32c0xx_hal_msp.c */
  i2c_hw_init();
}

static int i2c_wait(I2C_TypeDef *i2c, uint32_t mask, uint32_t t0)
{
  while ((i2c->ISR & (mask | I2C_ISR_NACKF)) == 0u) {
    if (HAL_GetTick() - t0 > I2C_TIMEOUT_MS) {
      return -1;
    }
  }
  return (i2c->ISR & I2C_ISR_NACKF) ? 1 : 0;
}

/** Шина вільна; для запису ще й прапорці контролера скинуті. false — BUSY не знявся за timeout. */
static bool i2c_begin(I2C_TypeDef *i2c, uint32_t t0, bool for_write)
{
  while (i2c->ISR & I2C_ISR_BUSY) {
    if (HAL_GetTick() - t0 > I2C_TIMEOUT_MS) {
      return false;
    }
  }
  if (!for_write) {
    i2c->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF; /* як HAL_I2C_Master_Receive: без скидання PE */
    return true;
  }
  /* Скидання прапорців контролера (PE=0 -> 1, RM0490 §25.4.5 «software reset»):
   * у Wokwi після попереднього читання TXIS лишається встановленим, і без цього
   * перший байт потрапляв у TXDR ще до адресної фази. На реальному кристалі скидання
   * лише очищає ISR і нічого не змінює на шині (шина вільна — BUSY перевірено вище). */
  i2c->CR1 &= ~I2C_CR1_PE;
  __DSB();
  i2c->CR1 |= I2C_CR1_PE;
  i2c->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
  return true;
}

/** Кінець транзакції з AUTOEND: дочекатися STOP (після NACK апаратура генерує його сама). */
static dev_status_t i2c_end(I2C_TypeDef *i2c, int r, uint32_t t0)
{
  if (r == 0) {
    r = i2c_wait(i2c, I2C_ISR_STOPF, t0);
  } else if (r == 1) {
    (void)i2c_wait(i2c, I2C_ISR_STOPF, t0);
  }
  i2c->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
  i2c->CR2 = 0;
  return r == 0 ? DEV_OK : (r == 1 ? DEV_ERR_NACK : DEV_ERR_TIMEOUT);
}

/** Фаза запису len байт; autoend=false — без STOP, далі repeated START (чекає TC). */
static int i2c_tx(I2C_TypeDef *i2c, uint8_t addr7, const uint8_t *data, size_t len, bool autoend, uint32_t t0)
{
  i2c->CR2 = ((uint32_t)addr7 << 1) | ((uint32_t)len << I2C_CR2_NBYTES_Pos) | (autoend ? I2C_CR2_AUTOEND : 0u)
             | I2C_CR2_START;
  int r = 0;
  for (size_t i = 0; i < len && r == 0; i++) {
    r = i2c_wait(i2c, I2C_ISR_TXIS, t0);
    if (r == 0) {
      i2c->TXDR = data[i];
    }
  }
  if (r == 0 && !autoend) {
    r = i2c_wait(i2c, I2C_ISR_TC, t0);
  }
  return r;
}

static int i2c_rx(I2C_TypeDef *i2c, uint8_t addr7, uint8_t *data, size_t len, uint32_t t0)
{
  i2c->CR2 = ((uint32_t)addr7 << 1) | I2C_CR2_RD_WRN | ((uint32_t)len << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND
             | I2C_CR2_START;
  int r = 0;
  for (size_t i = 0; i < len && r == 0; i++) {
    r = i2c_wait(i2c, I2C_ISR_RXNE, t0);
    if (r == 0) {
      data[i] = (uint8_t)i2c->RXDR;
    }
  }
  return r;
}

static dev_status_t i2c_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
  I2C_TypeDef *i2c = ((I2C_HandleTypeDef *)ctx)->Instance;
  uint32_t t0 = HAL_GetTick();
  if (len == 0u || len > 255u) {
    return DEV_ERR_PARAM;
  }
  if (!i2c_begin(i2c, t0, true)) {
    return DEV_ERR_BUS;
  }
  return i2c_end(i2c, i2c_tx(i2c, addr7, data, len, true, t0), t0);
}

/** Запис регістра і читання з repeated START (як HAL_I2C_Mem_Read). */
static dev_status_t i2c_read_restart(I2C_TypeDef *i2c, uint8_t addr7, const uint8_t *wdata, size_t wlen,
                                     uint8_t *rdata, size_t rlen)
{
  uint32_t t0 = HAL_GetTick();
  if (!i2c_begin(i2c, t0, true)) {
    return DEV_ERR_BUS;
  }
  int r = i2c_tx(i2c, addr7, wdata, wlen, false, t0);
  if (r == 0) {
    r = i2c_rx(i2c, addr7, rdata, rlen, t0);
  }
  return i2c_end(i2c, r, t0);
}

static dev_status_t i2c_write_read(void *ctx, uint8_t addr7, const uint8_t *wdata, size_t wlen,
                                   uint8_t *rdata, size_t rlen)
{
  I2C_TypeDef *i2c = ((I2C_HandleTypeDef *)ctx)->Instance;
  if (wlen == 0u || wlen > 255u || rlen == 0u || rlen > 255u) {
    return DEV_ERR_PARAM;
  }
#if BOARD_I2C_REPEATED_START
  return i2c_read_restart(i2c, addr7, wdata, wlen, rdata, rlen);
#else
  /* START addr+W reg STOP, START addr+R data... STOP. MPU-6050 зберігає вказівник регістра
   * між транзакціями, тому результат той самий, що й з repeated START. */
  dev_status_t w = i2c_write(ctx, addr7, wdata, wlen);
  if (w != DEV_OK) {
    return w;
  }
  uint32_t t0 = HAL_GetTick();
  if (!i2c_begin(i2c, t0, false)) {
    return DEV_ERR_BUS;
  }
  return i2c_end(i2c, i2c_rx(i2c, addr7, rdata, rlen, t0), t0);
#endif
}

/** Адреса без даних (NBYTES=0, AUTOEND): ACK — пристрій є, NACK — немає (як HAL_I2C_IsDeviceReady). */
static dev_status_t i2c_probe(void *ctx, uint8_t addr7)
{
  I2C_TypeDef *i2c = ((I2C_HandleTypeDef *)ctx)->Instance;
  uint32_t t0 = HAL_GetTick();
  if (!i2c_begin(i2c, t0, false)) {
    return DEV_ERR_BUS;
  }
  i2c->CR2 = ((uint32_t)addr7 << 1) | I2C_CR2_AUTOEND | I2C_CR2_START;
  return i2c_end(i2c, 0, t0);
}

const i2c_bus_t platform_i2c1 = {
  .write = i2c_write,
  .write_read = i2c_write_read,
  .probe = i2c_probe,
  .ctx = &hi2c1,
};

dev_status_t platform_i2c_mem_read_restart(uint8_t addr7, uint8_t reg, uint8_t *val)
{
  return i2c_read_restart(I2C1, addr7, &reg, 1, val, 1);
}

void platform_i2c_recover(void)
{
  /* Скидання лише PE не допомагає, коли обмін обірвався посередині: контролер тримає BUSY
   * (сценарій 6.2.4). Повна процедура: відпустити ведений дев'ятьма тактами SCL (UM10204 §3.1.16),
   * STOP, скинути блок через RCC, ініціалізувати заново. */
  GPIO_InitTypeDef g = { 0 };
  I2C1->CR1 = 0;
  g.Mode = GPIO_MODE_OUTPUT_OD;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Pin = I2C_SCL_PIN | I2C_SDA_PIN;
  HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN | I2C_SDA_PIN, GPIO_PIN_SET);
  board_gpio_init(I2C_SCL_PORT, &g);
  for (int i = 0; i < 9; i++) {
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
  }
  HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET); /* STOP: SDA вгору при високому SCL */
  HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
  __HAL_RCC_I2C1_FORCE_RESET();
  __HAL_RCC_I2C1_RELEASE_RESET();
  platform_i2c_init(); /* піни знову AF6 open-drain */
}

/* ---- SPI1 + CS# PB0 ------------------------------------------------------ */
static void spi_select(void *ctx, bool active)
{
  (void)ctx;
  HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, active ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/*
 * Обмін на регістрах SPI1 (ЛР4: HAL_SPI_TransmitReceive із супутніми функціями займав 1,5 КБ флеш,
 * потрібних ядру FreeRTOS). Послідовність та сама, що в HAL для 8 біт із FRXTH: байт у DR, чекати
 * RXNE, забрати прийнятий байт; наприкінці — BSY=0. Сигнали на шині не змінюються: SCK 3 МГц,
 * mode 0, CS# керує spi_select. У фазі читання на MOSI йдуть 0xFF (як і раніше, test_probe_matches_capture).
 */
static dev_status_t spi_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
  SPI_TypeDef *spi = ((SPI_HandleTypeDef *)ctx)->Instance;
  volatile uint8_t *dr = (volatile uint8_t *)&spi->DR; /* 8-бітний доступ: один кадр у FIFO */
  uint32_t t0 = HAL_GetTick();
  if ((spi->CR1 & SPI_CR1_SPE) == 0u) {
    spi->CR1 |= SPI_CR1_SPE;
  }
  for (size_t i = 0; i < len; i++) {
    while ((spi->SR & SPI_SR_TXE) == 0u) {
      if (HAL_GetTick() - t0 > SPI_TIMEOUT_MS) {
        return DEV_ERR_TIMEOUT;
      }
    }
    *dr = tx != NULL ? tx[i] : 0xFFu;
    while ((spi->SR & SPI_SR_RXNE) == 0u) {
      if (HAL_GetTick() - t0 > SPI_TIMEOUT_MS) {
        return DEV_ERR_TIMEOUT;
      }
    }
    uint8_t b = *dr;
    if (rx != NULL) {
      rx[i] = b;
    }
  }
  while (spi->SR & SPI_SR_BSY) {
    if (HAL_GetTick() - t0 > SPI_TIMEOUT_MS) {
      return DEV_ERR_TIMEOUT;
    }
  }
  return DEV_OK;
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
uint32_t platform_millis(void) { return HAL_GetTick(); }
static void delay_ms(uint32_t ms) { HAL_Delay(ms); }

const dev_time_t platform_time = { platform_millis, delay_ms };

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
/* Передача байтами через TDR з очікуванням TC наприкінці — те саме, що HAL_UART_Transmit (ЛР4: без
 * HAL_UART_Init хендл не ініціалізований). Час визначає швидкість лінії: 86,8 мкс на байт. */
static dev_status_t uart_tx(USART_TypeDef *u, const char *s, size_t len)
{
  uint32_t t0 = HAL_GetTick();
  for (size_t i = 0; i < len; i++) {
    while ((u->ISR & USART_ISR_TXE_TXFNF) == 0u) {
      if (HAL_GetTick() - t0 > 100u) {
        return DEV_ERR_TIMEOUT;
      }
    }
    u->TDR = (uint8_t)s[i];
  }
  while ((u->ISR & USART_ISR_TC) == 0u) {
    if (HAL_GetTick() - t0 > 100u) {
      return DEV_ERR_TIMEOUT;
    }
  }
  return DEV_OK;
}

void platform_con_write(const char *s, size_t len)
{
  (void)uart_tx(USART2, s, len);
}

/* Прийом консолі: переривання RXNE -> кільцевий буфер. Опитування RXNE з головного циклу
 * втрачало байти (ORE) під час 14-байтового читання MPU-6050 у режимі LIVE. */
#define CON_RX_BUF 64u /* степінь двійки */
static volatile uint8_t con_rx_buf[CON_RX_BUF];
static volatile uint8_t con_rx_head;
static volatile uint8_t con_rx_tail;
static volatile uint32_t con_rx_dropped;

/* Мітки часу кінця рядка для вимірювання «команда -> відповідь» */
#define EOL_FIFO 8u /* степінь двійки */
static volatile uint32_t eol_t[EOL_FIFO];
static volatile uint8_t eol_head;
static volatile uint8_t eol_tail;

void platform_con_start_rx(void)
{
  USART2->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
  HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void platform_con_rx_isr(void)
{
  USART_TypeDef *u = USART2;
  if (u->ISR & USART_ISR_ORE) {
    u->ICR = USART_ICR_ORECF;
    con_rx_dropped++;
  }
  if (u->ISR & USART_ISR_RXNE_RXFNE) {
    uint8_t b = (uint8_t)(u->RDR & 0xFFu);
    uint8_t next = (uint8_t)((con_rx_head + 1u) & (CON_RX_BUF - 1u));
    if (next != con_rx_tail) { /* при переповненні байт відкидається */
      con_rx_buf[con_rx_head] = b;
      con_rx_head = next;
    } else {
      con_rx_dropped++;
    }
    if (b == '\n' && ((eol_head + 1u) & (EOL_FIFO - 1u)) != eol_tail) {
      eol_t[eol_head] = platform_cycles();
      eol_head = (uint8_t)((eol_head + 1u) & (EOL_FIFO - 1u));
    }
  }
}

bool platform_rx_eol_take(uint32_t *t_cycles)
{
  if (eol_tail == eol_head) {
    return false;
  }
  *t_cycles = eol_t[eol_tail];
  eol_tail = (uint8_t)((eol_tail + 1u) & (EOL_FIFO - 1u));
  return true;
}

uint32_t platform_rx_dropped(void) { return con_rx_dropped; }

int platform_con_getc(void)
{
  if (con_rx_tail == con_rx_head) {
    return -1;
  }
  int c = con_rx_buf[con_rx_tail];
  con_rx_tail = (uint8_t)((con_rx_tail + 1u) & (CON_RX_BUF - 1u));
  return c;
}

dev_status_t platform_aux_write(const char *s, size_t len)
{
  return uart_tx(USART1, s, len);
}

uint32_t platform_sysclk_hz(void)
{
  return SystemCoreClock; /* оновлює SystemCoreClockUpdate() у SystemClock_Config */
}

/* ---- База часу HAL на TIM14 (ЛР4) ---------------------------------------- */
#define TICK_CYCLES (BOARD_SYSCLK_HZ / 1000u)

/*
 * SysTick тепер належить FreeRTOS (тік ОС), тому HAL переходить на TIM14, як «Timebase Source»
 * у CubeMX (методичка ЛР4 §4.10). TIM14 рахує вгору від 0 до TICK_CYCLES-1 на частоті ядра:
 * переповнення раз на 1 мс оновлює uwTick, а CNT дає такти всередині мілісекунди. Ця пара
 * замінює DWT CYCCNT, якого на Cortex-M0+ немає. Перевизначає слабку HAL_InitTick().
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  __HAL_RCC_TIM14_CLK_ENABLE();
  TIM14->CR1 = 0;
  TIM14->PSC = 0;
  TIM14->ARR = SystemCoreClock / 1000u - 1u; /* до SystemClock_Config ядро працює на 12 МГц */
  TIM14->EGR = TIM_EGR_UG;
  TIM14->SR = 0;
  TIM14->DIER = TIM_DIER_UIE;
  HAL_NVIC_SetPriority(TIM14_IRQn, TickPriority, 0);
  HAL_NVIC_EnableIRQ(TIM14_IRQn);
  TIM14->CR1 = TIM_CR1_CEN;
  uwTickPrio = TickPriority;
  return HAL_OK;
}

void HAL_SuspendTick(void) { TIM14->DIER &= ~TIM_DIER_UIE; }
void HAL_ResumeTick(void) { TIM14->DIER |= TIM_DIER_UIE; }

void platform_tick_isr(void)
{
  if (TIM14->SR & TIM_SR_UIF) {
    TIM14->SR = ~TIM_SR_UIF;
    HAL_IncTick();
  }
}

/* ---- Вимірювання (ЛР3) --------------------------------------------------- */
/*
 * Пара (uwTick, TIM14->CNT) узгоджена, якщо uwTick не змінився між читаннями. TIM14 має
 * найвищий пріоритет (TICK_INT_PRIORITY = 0), але критична секція FreeRTOS на Cortex-M0+
 * (cpsid i) затримує і його. Тоді переповнення вже сталося, а uwTick ще старий: це видно з
 * прапорця UIF, і до мілісекунд додається одиниця, а CNT читається вже після переносу.
 */
static void tick_read(uint32_t *ms, uint32_t *elapsed)
{
  uint32_t t;
  uint32_t c;
  uint32_t pend;
  do {
    t = HAL_GetTick();
    c = TIM14->CNT;
    pend = TIM14->SR & TIM_SR_UIF;
    if (pend) {
      c = TIM14->CNT;
    }
  } while (t != HAL_GetTick());
  *ms = pend ? t + 1u : t;
  *elapsed = c;
}

uint32_t platform_cycles(void)
{
  uint32_t ms;
  uint32_t el;
  tick_read(&ms, &el);
  return ms * TICK_CYCLES + el;
}

uint32_t platform_micros(void)
{
  uint32_t ms;
  uint32_t el;
  tick_read(&ms, &el);
  return ms * 1000u + el / (TICK_CYCLES / 1000u);
}

static GPIO_TypeDef *const probe_port[PROBE_COUNT] = { PROBE0_PORT, PROBE1_PORT, PROBE2_PORT, PROBE3_PORT };
static const uint16_t probe_pin[PROBE_COUNT] = { PROBE0_PIN, PROBE1_PIN, PROBE2_PIN, PROBE3_PIN };

void platform_probe(unsigned n, bool level)
{
  /* BSRR — атомарний запис одним тактом шини, як PROBE_ON/PROBE_OFF у методичці §5.3 */
  probe_port[n]->BSRR = level ? (uint32_t)probe_pin[n] : ((uint32_t)probe_pin[n] << 16);
}

static volatile uint32_t btn_stamp;
static volatile uint8_t btn_pending;

void platform_btn_isr(void)
{
  if (__HAL_GPIO_EXTI_GET_FALLING_IT(BTN_PIN)) {
    __HAL_GPIO_EXTI_CLEAR_FALLING_IT(BTN_PIN);
    if (!btn_pending) { /* наступні спади до обробки — брязкіт або нове натискання: втрачаються */
      btn_stamp = platform_cycles();
      btn_pending = 1;
    }
  }
}

bool platform_btn_take(uint32_t *t_cycles)
{
  if (!btn_pending) {
    return false;
  }
  /* Порядок важливий: мітка читається до скидання прапорця, а ISR пише її лише при pending=0.
   * volatile тут лише забороняє компілятору кешувати значення; саму пару (мітка, прапорець)
   * робить узгодженою ця послідовність. */
  *t_cycles = btn_stamp;
  btn_pending = 0;
  return true;
}

/* ---- Стек (методичка §5.4) ---------------------------------------- */
extern uint32_t _estack;
extern uint32_t _Min_Stack_Size; /* абсолютний символ: його адреса = розмір */
#define STACK_PATTERN 0xDEADBEEFu

static uint32_t *stack_bottom(void)
{
  return (uint32_t *)((uint32_t)&_estack - (uint32_t)&_Min_Stack_Size);
}

void platform_stack_paint(void)
{
  uint32_t *p = stack_bottom();
  uint32_t *sp = (uint32_t *)__get_MSP();
  while (p < sp - 16) { /* поточний кадр не чіпаємо */
    *p++ = STACK_PATTERN;
  }
}

uint32_t platform_stack_used(void)
{
  uint32_t *p = stack_bottom();
  while (p < &_estack && *p == STACK_PATTERN) {
    p++;
  }
  return (uint32_t)&_estack - (uint32_t)p;
}

uint32_t platform_stack_size(void) { return (uint32_t)&_Min_Stack_Size; }

void platform_reset(void) { NVIC_SystemReset(); }

/** Калібрування швидкості ядра: 500 ітерацій subs+bne, номінально 3-4 такти на ітерацію. */
uint32_t platform_cal_loop(void)
{
  uint32_t t0 = platform_cycles();
  __asm volatile(".syntax unified\n movs r0, #250\n lsls r0, r0, #1\n 1: subs r0, r0, #1\n bne 1b\n"
                 : : : "r0", "cc");
  return platform_cycles() - t0;
}
