#include "rtos_app.h"

#include <stdarg.h>

#include "FreeRTOS.h"
#include "app_internal.h"
#include "app_io.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/* ---- Задачі: пріоритет за жорсткістю дедлайну (rate/deadline-monotonic) --- */
enum {
  PRIO_LONG = 1,  /* dump, erase: сотні мс, поступається всім */
  PRIO_TRAJ = 2,  /* запис сторінок траєкторії у флеш: можна відкласти на кілька семплів */
  PRIO_LOG = 2,   /* відкладений друк для imu */
  PRIO_CLI = 3,   /* людина чекає відповідь, мілісекунди не критичні */
  PRIO_SERVO = 4, /* період 20 мс, FR-03/FR-04 */
  PRIO_IMU = 5,   /* період 5 мс ± 0,5 мс, FR-01: найжорсткіший період */
  PRIO_BTN = 6,   /* реакція на подію: найкоротша робота, має випереджати все */
};

/* Стеки в словах (4 Б). Стартові значення перевірено командою stack після S3 (docs/rtos-design.md). */
#ifndef RTOS_STACK_IMU
#define RTOS_STACK_IMU 160
#endif
enum {
  STACK_BTN = 112, STACK_IMU = RTOS_STACK_IMU, STACK_SERVO = 160, STACK_CLI = 320, /* selftest у cli */
  STACK_TRAJ = 192, STACK_LOG = 96, STACK_LONG = 176,
};

/* Номери задач для trace: 0 — idle (ядро не задає номер) */
enum { TN_IDLE = 0, TN_BTN, TN_IMU, TN_SERVO, TN_CLI, TN_TRAJ, TN_LOG, TN_LONG, TN_COUNT };
static const char *const tn_name[TN_COUNT] = { "IDLE", "btn", "imu", "servo", "cli", "traj", "log", "long" };
static const uint16_t tn_stack[TN_COUNT] = { configMINIMAL_STACK_SIZE, STACK_BTN, STACK_IMU, STACK_SERVO,
                                             STACK_CLI, STACK_TRAJ, STACK_LOG, STACK_LONG };

#define NOTE_BTN  0x1u /* EXTI13: натиснуто B1 */
#define NOTE_RX   0x1u /* USART2: прийнято байт (сповіщення задачі cli) */
#define NOTE_PING 0x2u /* вимірювання перемикання контексту (ctxsw) */

#define LOG_DEPTH   4u
#define LOG_LEN     96u
#define TRAJ_DEPTH  16u /* 0,32 с семплів: перекриває стирання сектора (45 мс) із запасом */

typedef struct {
  uint8_t len;     /* 0 — службовий запис для сценаріїв fault (spin) */
  char s[LOG_LEN];
} log_rec_t;

enum { MSG_SAMPLE = 0, MSG_MODE };
typedef struct {
  uint8_t type;
  uint8_t mode;
  int16_t pan, tilt;
} traj_msg_t;

/* long: значення сповіщення = код операції + 1 */
enum { LONG_OP_DUMP = 1, LONG_OP_ERASE, LONG_OP_INV_MUTEX, LONG_OP_INV_SEM, LONG_OP_STARVE_FLAG };

static app_t *g_app;
static const app_hw_t *g_hw;
static TaskHandle_t h_btn, h_imu, h_servo, h_cli, h_traj, h_log, h_long;
static QueueHandle_t q_log, q_traj;
static SemaphoreHandle_t locks[APP_LOCK_COUNT];
#if RTOS_FAULTS
static SemaphoreHandle_t inv_mutex, inv_sem; /* ресурс для сценарію інверсії пріоритетів (§7.2.1) */
#endif

/* Лічильники ОС */
static volatile uint32_t log_lost, rec_lost, mode_lost;
static volatile uint32_t ping_t1;
static perf_stat_t tick_cost;

/* ---- Дрібниці ---------------------------------------------------------- */
static bool running(void) { return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING; }

uint32_t rtos_tick_us(void) { return 1000000u / configTICK_RATE_HZ; }

/** Період у тіках, не менше одного: vTaskDelayUntil рахує цілими тіками. */
static TickType_t ticks_for_us(uint32_t us)
{
  uint32_t t = (us + rtos_tick_us() / 2u) / rtos_tick_us();
  return (TickType_t)(t == 0u ? 1u : t);
}

static TickType_t ticks_for_ms(uint32_t ms)
{
  return ms == APP_WAIT_FOREVER ? portMAX_DELAY : (TickType_t)((ms * configTICK_RATE_HZ + 999u) / 1000u);
}

#if RTOS_FAULTS
static void busy_us(uint32_t us)
{
  uint32_t t0 = g_hw->cycles();
  while (g_hw->cycles() - t0 < us * g_app->cyc_per_us) {
  }
}
#endif

static void con_line(const char *fmt, ...) APP_PRINTF_FMT(1, 2);
static void con_line(const char *fmt, ...)
{
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  int n = app_vformat(buf, sizeof buf, fmt, ap);
  va_end(ap);
  g_hw->con_write(buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1u);
}

/* FreeRTOS (vTaskGetRunTimeStatistics) потребує snprintf. newlib-nano тягне за собою ~4 КБ
 * (власний форматер, docs/metrics/memory.md), тому тут — тонка обгортка над app_vformat. */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int n = app_vformat(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

/* ---- Реалізації app_hw_t ------------------------------------------------ */
bool rtos_lock(app_lock_t id, uint32_t timeout_ms)
{
  if (!running()) {
    return true; /* до старту працює одна гілка виконання */
  }
  return xSemaphoreTake(locks[id], ticks_for_ms(timeout_ms)) == pdTRUE;
}

void rtos_unlock(app_lock_t id)
{
  if (running()) {
    (void)xSemaphoreGive(locks[id]);
  }
}

void rtos_crit(bool enter)
{
  if (!running()) {
    return;
  }
  if (enter) {
    taskENTER_CRITICAL();
  } else {
    taskEXIT_CRITICAL();
  }
}

void rtos_log_post(const char *s, size_t len)
{
  if (!running()) {
    g_hw->con_write(s, len);
    return;
  }
  log_rec_t r;
  r.len = (uint8_t)(len < LOG_LEN ? len : LOG_LEN);
  for (size_t i = 0; i < r.len; i++) {
    r.s[i] = s[i];
  }
  if (xQueueSend(q_log, &r, 0) != pdTRUE) {
    log_lost++; /* imu не чекає: рядок втрачено, лічильник у tasks */
  }
}

void rtos_mode_post(int mode)
{
  traj_msg_t m = { MSG_MODE, (uint8_t)mode, 0, 0 };
  if (xQueueSend(q_traj, &m, ticks_for_ms(10u)) != pdTRUE) {
    mode_lost++;
  }
}

void rtos_rec_post(int16_t pan, int16_t tilt)
{
  traj_msg_t m = { MSG_SAMPLE, 0, pan, tilt };
  if (xQueueSend(q_traj, &m, 0) != pdTRUE) {
    rec_lost++; /* servo не чекає: семпл втрачено */
  }
}

void rtos_long_post(app_long_op_t op)
{
  (void)xTaskNotify(h_long, (uint32_t)op + 1u, eSetValueWithOverwrite);
}

bool rtos_delay_ms(uint32_t ms)
{
  if (!running()) {
    return false;
  }
  vTaskDelay(ticks_for_ms(ms));
  return true;
}

/* ---- Переривання -------------------------------------------------------- */
void rtos_btn_from_isr(void)
{
  BaseType_t woken = pdFALSE;
  if (h_btn != NULL) {
    (void)xTaskNotifyFromISR(h_btn, NOTE_BTN, eSetBits, &woken);
  }
  portYIELD_FROM_ISR(woken);
}

void rtos_con_rx_from_isr(void)
{
  BaseType_t woken = pdFALSE;
  if (h_cli != NULL) {
    (void)xTaskNotifyFromISR(h_cli, NOTE_RX, eSetBits, &woken);
  }
  portYIELD_FROM_ISR(woken);
}

void rtos_tick_cost(uint32_t cycles) { perf_add(&tick_cost, cycles); }

/* ---- Хронологія і завантаження (traceTASK_SWITCHED_IN, tick hook) -------- */
#if RTOS_FAULTS
/* Кільцевий буфер хронології — у варіанті зі сценаріями ОС: у продуктову збірку не вміщається
 * (флеш 99,8 %); там хронологію показують службові піни на логічному аналізаторі. */
#define TRACE_LEN  32u
#define TRACE_MASK 0x0FFFFFFFu /* запис — одне слово: номер задачі в 4 старших бітах, мкс — у 28 молодших */

static uint32_t trace_buf[TRACE_LEN];
static volatile uint8_t trace_state; /* 0 — вимкнено, 1 — чекає B1, 2 — пише, 3 — заповнено */
static volatile uint8_t trace_n;
#endif
static uint32_t idle_acc_us, idle_since_us;
static volatile uint32_t switches; /* перемикань контексту з моменту старту (накладні витрати, §6.5) */
static uint8_t cur_task;

void rtos_trace_switched_in(unsigned task_number)
{
  uint32_t t = platform_micros();
  switches++;
  if (cur_task == TN_IDLE) {
    idle_acc_us += t - idle_since_us;
  }
  cur_task = (uint8_t)task_number;
  if (cur_task == TN_IDLE) {
    idle_since_us = t;
  }
#if RTOS_FAULTS
  if (trace_state == 2u) {
    trace_buf[trace_n] = ((uint32_t)cur_task << 28) | (t & TRACE_MASK);
    if (++trace_n == TRACE_LEN) {
      trace_state = 3u;
    }
  }
#endif
}

void vApplicationTickHook(void)
{
  static uint32_t ticks, t0_us, idle0_us;
  if (++ticks < configTICK_RATE_HZ) {
    return;
  }
  ticks = 0;
  uint32_t t = platform_micros();
  uint32_t idle = idle_acc_us + (cur_task == TN_IDLE ? t - idle_since_us : 0u);
  uint32_t dt = t - t0_us;
  uint32_t di = idle - idle0_us;
  if (dt != 0u && t0_us != 0u) {
    app_load_update(g_app, (uint8_t)(di >= dt ? 0u : 100u - (uint32_t)(((uint64_t)di * 100u) / dt)));
  }
  t0_us = t;
  idle0_us = idle;
}

/* ---- Хуки діагностики (§4.11) ------------------------------------------- */
void vApplicationStackOverflowHook(TaskHandle_t t, char *name)
{
  (void)t;
  taskDISABLE_INTERRUPTS();
  con_line("FATAL stack overflow in %s\r\n", name);
  for (;;) {
  }
}

void vApplicationMallocFailedHook(void)
{
  taskDISABLE_INTERRUPTS();
  con_line("FATAL malloc failed,heap_total=%u,free=%u\r\n", (unsigned)configTOTAL_HEAP_SIZE,
           (unsigned)xPortGetFreeHeapSize());
  for (;;) {
  }
}

void rtos_assert_failed(int line)
{
  taskDISABLE_INTERRUPTS();
  con_line("FATAL assert,line=%d\r\n", line);
  for (;;) {
  }
}

/* ---- Задачі -------------------------------------------------------------- */
static void btn_task(void *arg)
{
  (void)arg;
  portENABLE_INTERRUPTS(); /* перший запуск: ядро стартує з вимкненими перериваннями (Wokwi — емуляція) */
  for (;;) {
    uint32_t bits = 0;
    (void)xTaskNotifyWait(0, 0xFFFFFFFFu, &bits, portMAX_DELAY);
    uint32_t now = g_hw->cycles(); /* перша інструкція після пробудження (§6.5) */
    if (bits & NOTE_PING) {
      ping_t1 = now;
    }
    uint32_t t_event;
    if ((bits & NOTE_BTN) && g_hw->btn_take(&t_event)) {
#if RTOS_FAULTS
      if (trace_state == 1u) {
        trace_n = 0;
        trace_state = 2u; /* хронологія з моменту натискання */
      }
#endif
      app_btn_event(g_app, t_event);
      /* Брязкіт: чекати 20 мс стабільно відпущеної кнопки, задача при цьому заблокована */
      uint32_t quiet_ms = 0;
      while (quiet_ms < APP_BTN_RELEASE_MS) {
        vTaskDelay(1);
        quiet_ms = g_hw->button_pressed() ? 0u : quiet_ms + rtos_tick_us() / 1000u;
      }
      (void)g_hw->btn_take(&t_event);
    }
  }
}

static void imu_task(void *arg)
{
  (void)arg;
  portENABLE_INTERRUPTS();
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    /* Пропущені періоди не наздоганяються, як і в ЛР3: після запізнення відлік — від поточного тіку */
    if (xTaskDelayUntil(&last, ticks_for_us(g_app->imu_period_us)) == pdFALSE) {
      last = xTaskGetTickCount();
    }
    app_imu_step(g_app, g_hw->cycles()); /* джитер — одразу після vTaskDelayUntil (§6.1) */
  }
}

static void servo_task(void *arg)
{
  (void)arg;
  portENABLE_INTERRUPTS();
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    if (xTaskDelayUntil(&last, ticks_for_us(APP_SERVO_PERIOD_US)) == pdFALSE) {
      last = xTaskGetTickCount();
    }
    app_servo_step(g_app, g_hw->cycles());
  }
}

static void cli_task(void *arg)
{
  (void)arg;
  portENABLE_INTERRUPTS();
  for (;;) {
    uint32_t bits = 0;
    (void)xTaskNotifyWait(0, 0xFFFFFFFFu, &bits, portMAX_DELAY);
    app_cli_step(g_app);
  }
}

static void traj_task(void *arg)
{
  (void)arg;
  portENABLE_INTERRUPTS();
  for (;;) {
    traj_msg_t m;
    (void)xQueueReceive(q_traj, &m, portMAX_DELAY);
    if (m.type == MSG_SAMPLE) {
      app_rec_add(g_app, m.pan, m.tilt);
      continue;
    }
    /* REC і PLAY працюють із флеш: якщо йде dump або erase, вхід чекає їхнього кінця (як у ЛР3,
     * де кнопка під час dump спрацьовувала після нього) */
    while ((m.mode == APP_MODE_REC || m.mode == APP_MODE_PLAY) && g_app->long_busy) {
      vTaskDelay(ticks_for_ms(5u));
    }
    app_set_mode(g_app, (app_mode_t)m.mode);
  }
}

static void log_task(void *arg)
{
  (void)arg;
  portENABLE_INTERRUPTS();
  for (;;) {
    log_rec_t r;
    (void)xQueueReceive(q_log, &r, portMAX_DELAY);
#if RTOS_FAULTS
    if (r.len == 0u) {
      busy_us(100000u); /* «середня» задача сценарію інверсії: 100 мс процесора без блокування */
      continue;
    }
#endif
    (void)rtos_lock(APP_LOCK_CON, APP_WAIT_FOREVER);
    g_hw->con_write(r.s, r.len);
    rtos_unlock(APP_LOCK_CON);
  }
}

#if RTOS_FAULTS
static volatile uint8_t starve_flag;
static volatile uint8_t inv_held; /* long уже тримає ресурс сценарію інверсії */
#endif

static void long_task(void *arg)
{
  (void)arg;
  portENABLE_INTERRUPTS();
  for (;;) {
    uint32_t op = 0;
    (void)xTaskNotifyWait(0, 0xFFFFFFFFu, &op, portMAX_DELAY);
    if (op == LONG_OP_DUMP || op == LONG_OP_ERASE) {
      app_long_run(g_app, op == LONG_OP_DUMP ? APP_LONG_DUMP : APP_LONG_ERASE);
    }
#if RTOS_FAULTS
    else if (op == LONG_OP_INV_MUTEX || op == LONG_OP_INV_SEM) {
      SemaphoreHandle_t res = op == LONG_OP_INV_MUTEX ? inv_mutex : inv_sem;
      (void)xSemaphoreTake(res, portMAX_DELAY);
      inv_held = 1;
      g_hw->probe(APP_PROBE_LONG, true);
      busy_us(50000u); /* «низька» задача тримає ресурс 50 мс процесорного часу */
      g_hw->probe(APP_PROBE_LONG, false);
      inv_held = 0;
      (void)xSemaphoreGive(res);
    } else if (op == LONG_OP_STARVE_FLAG) {
      starve_flag = 1; /* виставить прапорець, лише якщо отримає процесор */
    }
#endif
  }
}

/* ---- Команди ОС ------------------------------------------------------------ */
static char stats_buf[TN_COUNT * 28u]; /* рядок vTaskGetRunTimeStatistics ≈ 22 символи */

static void cmd_tasks(void)
{
  vTaskGetRunTimeStatistics(stats_buf, sizeof stats_buf); /* призупиняє планувальник на час обходу */
  (void)rtos_lock(APP_LOCK_CON, APP_WAIT_FOREVER);
  con_line("TASKS,BEGIN,t_us=%lu\r\n", (unsigned long)platform_micros());
  size_t n = 0;
  while (stats_buf[n] != '\0') {
    n++;
  }
  g_hw->con_write(stats_buf, n);
  con_line("OSCNT log_lost=%lu rec_lost=%lu mode_lost=%lu i2c_busy=%lu sw=%lu\r\n", (unsigned long)log_lost,
           (unsigned long)rec_lost, (unsigned long)mode_lost, (unsigned long)g_app->i2c_busy,
           (unsigned long)switches);
  con_line("TASKS,END\r\n");
  rtos_unlock(APP_LOCK_CON);
}

static bool name_eq(const char *a, const char *b)
{
  while (*a != '\0' && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

static void cmd_stack(void)
{
  static TaskStatus_t st[TN_COUNT]; /* 288 Б: на стеку cli лишалось 10 вільних слів (журнал проблем) */
  UBaseType_t n = uxTaskGetSystemState(st, TN_COUNT, NULL);
  for (UBaseType_t i = 0; i < n; i++) {
    unsigned tn = TN_COUNT;
    while (tn-- > 0u && !name_eq(st[i].pcTaskName, tn_name[tn])) {
    }
    app_print(g_hw, "STACK task=%s free_min_words=%u size_words=%u\r\n", st[i].pcTaskName,
              (unsigned)st[i].usStackHighWaterMark, (unsigned)tn_stack[tn < TN_COUNT ? tn : 0u]);
  }
  app_print(g_hw, "STACK task=MSP used=%lu reserved=%lu\r\n", (unsigned long)g_hw->stack_used(),
            (unsigned long)g_hw->stack_size());
}

static void cmd_heap(void)
{
  app_print(g_hw, "HEAP free=%u min_ever_free=%u total=%u\r\n", (unsigned)xPortGetFreeHeapSize(),
            (unsigned)xPortGetMinimumEverFreeHeapSize(), (unsigned)configTOTAL_HEAP_SIZE);
}

static void cmd_ctxsw(void)
{
  enum { N = 1000 };
  perf_stat_t sw = { 0, 0, 0, 0 };
  for (int i = 0; i < N; i++) {
    uint32_t t0 = g_hw->cycles();
    (void)xTaskNotify(h_btn, NOTE_PING, eSetBits); /* btn має вищий пріоритет: витісняє одразу */
    perf_add(&sw, ping_t1 - t0);
  }
  app_print(g_hw, "CTXSW n=%lu min_cyc=%lu avg_cyc=%lu max_cyc=%lu\r\n", (unsigned long)sw.count,
            (unsigned long)sw.min, (unsigned long)perf_avg(&sw), (unsigned long)sw.max);
  rtos_crit(true);
  perf_stat_t tc = tick_cost;
  rtos_crit(false);
  app_print(g_hw, "OSTICK hz=%u n=%lu min_cyc=%lu avg_cyc=%lu max_cyc=%lu\r\n", (unsigned)configTICK_RATE_HZ,
            (unsigned long)tc.count, (unsigned long)tc.min, (unsigned long)perf_avg(&tc), (unsigned long)tc.max);
}

#if RTOS_FAULTS
static void cmd_trace(void)
{
  if (trace_state != 3u) {
    trace_n = 0;
    trace_state = 1u;
    app_print(g_hw, "TRACE,armed\r\n"); /* запис почнеться з наступного натискання B1 */
    return;
  }
  app_print(g_hw, "TRACE,BEGIN,n=%u\r\n", (unsigned)TRACE_LEN);
  for (unsigned i = 0; i < TRACE_LEN; i++) {
    uint32_t task = trace_buf[i] >> 28;
    app_print(g_hw, "TRACE t_us=%lu task=%s\r\n", (unsigned long)(trace_buf[i] & TRACE_MASK),
              tn_name[task < TN_COUNT ? task : 0u]);
  }
  app_print(g_hw, "TRACE,END\r\n");
  trace_state = 0;
}
#endif

#if RTOS_FAULTS
/* §7.2.1: cli (3) чекає ресурс, який тримає long (1), а log (2) займає процесор 100 мс */
static void cmd_inversion(bool use_mutex)
{
  SemaphoreHandle_t res = use_mutex ? inv_mutex : inv_sem;
  (void)xTaskNotify(h_long, use_mutex ? LONG_OP_INV_MUTEX : LONG_OP_INV_SEM, eSetValueWithOverwrite);
  for (int i = 0; i < 500 && !inv_held; i++) {
    vTaskDelay(1); /* long бере ресурс, щойно отримає процесор (попередній дослід міг ще займати log) */
  }
  vTaskDelay(ticks_for_ms(5u)); /* long тримає ресурс і працює */
  log_rec_t spin = { 0, { 0 } };
  (void)xQueueSend(q_log, &spin, 0); /* «середня» задача стає готовою */
  uint32_t t0 = g_hw->cycles();
  (void)xSemaphoreTake(res, portMAX_DELAY);
  uint32_t wait_us = (g_hw->cycles() - t0) / g_app->cyc_per_us;
  (void)xSemaphoreGive(res);
  app_print(g_hw, "INV,kind=%s,hold_us=50000,medium_us=100000,wait_us=%lu\r\n", use_mutex ? "mutex" : "binsem",
            (unsigned long)wait_us);
}

/* §7.2.3: cli активно чекає прапорця, який може виставити лише long — нижчий пріоритет */
static void cmd_starve(void)
{
  starve_flag = 0;
  (void)xTaskNotify(h_long, LONG_OP_STARVE_FLAG, eSetValueWithOverwrite);
  uint32_t t0 = g_hw->cycles();
  while (!starve_flag && g_hw->cycles() - t0 < 3000u * 1000u * g_app->cyc_per_us) {
  }
  uint32_t waited_ms = (g_hw->cycles() - t0) / (1000u * g_app->cyc_per_us);
  app_print(g_hw, "STARVE,flag_seen=%u,waited_ms=%lu\r\n", (unsigned)starve_flag, (unsigned long)waited_ms);
}

/* §7.2.4: 50 записів у чергу log глибиною 4 — без очікування і з timeout 20 мс */
static void cmd_qflood(void)
{
  for (int pass = 0; pass < 2; pass++) {
    TickType_t wait = pass == 0 ? 0 : ticks_for_ms(20u);
    uint32_t sent = 0;
    uint32_t lost = 0;
    uint32_t t0 = g_hw->cycles();
    for (int i = 0; i < 50; i++) {
      log_rec_t r;
      r.len = (uint8_t)app_format(r.s, sizeof r.s, "QF,pass=%d,i=%d\r\n", pass, i);
      if (xQueueSend(q_log, &r, wait) == pdTRUE) {
        sent++;
      } else {
        lost++;
      }
    }
    uint32_t us = (g_hw->cycles() - t0) / g_app->cyc_per_us;
    vTaskDelay(ticks_for_ms(100u)); /* дати log вивести чергу */
    app_print(g_hw, "QFLOOD,timeout_ms=%u,depth=%u,sent=%lu,lost=%lu,producer_us=%lu\r\n", pass == 0 ? 0u : 20u,
              (unsigned)LOG_DEPTH, (unsigned long)sent, (unsigned long)lost, (unsigned long)us);
  }
}

#endif /* RTOS_FAULTS */

void rtos_os_cmd(int id)
{
  switch ((cli_cmd_id_t)id) {
    case CLI_TASKS: cmd_tasks(); break;
    case CLI_STACK: cmd_stack(); break;
    case CLI_HEAP: cmd_heap(); break;
    case CLI_CTXSW: cmd_ctxsw(); break;
#if RTOS_FAULTS
    case CLI_TRACE: cmd_trace(); break;
    case CLI_FAULT_INV_MUTEX: cmd_inversion(true); break;
    case CLI_FAULT_INV_SEM: cmd_inversion(false); break;
    case CLI_FAULT_STARVE: cmd_starve(); break;
    default: cmd_qflood(); break;
#else
    default: app_print(g_hw, "ERR,needs_build_faults\r\n"); break; /* make RTOS_DEFS=-DRTOS_FAULTS=1 */
#endif
  }
}

/* ---- Старт ------------------------------------------------------------------ */
static TaskHandle_t create(TaskFunction_t fn, const char *name, uint16_t stack, UBaseType_t prio, unsigned tn)
{
  TaskHandle_t h = NULL;
  BaseType_t ok = xTaskCreate(fn, name, stack, NULL, prio, &h);
  configASSERT(ok == pdPASS);
  vTaskSetTaskNumber(h, tn);
  return h;
}

void rtos_start(app_t *app, const app_hw_t *hw)
{
  g_app = app;
  g_hw = hw;
  for (int i = 0; i < APP_LOCK_COUNT; i++) {
    locks[i] = xSemaphoreCreateMutex();
    configASSERT(locks[i] != NULL);
  }
#if RTOS_FAULTS
  inv_mutex = xSemaphoreCreateMutex();
  inv_sem = xSemaphoreCreateBinary();
  configASSERT(inv_mutex != NULL && inv_sem != NULL);
  (void)xSemaphoreGive(inv_sem);
#endif
  q_log = xQueueCreate(LOG_DEPTH, sizeof(log_rec_t));
  q_traj = xQueueCreate(TRAJ_DEPTH, sizeof(traj_msg_t));
  configASSERT(q_log != NULL && q_traj != NULL);

  h_btn = create(btn_task, "btn", STACK_BTN, PRIO_BTN, TN_BTN);
  h_imu = create(imu_task, "imu", STACK_IMU, PRIO_IMU, TN_IMU);
  h_servo = create(servo_task, "servo", STACK_SERVO, PRIO_SERVO, TN_SERVO);
  h_cli = create(cli_task, "cli", STACK_CLI, PRIO_CLI, TN_CLI);
  h_traj = create(traj_task, "traj", STACK_TRAJ, PRIO_TRAJ, TN_TRAJ);
  h_log = create(log_task, "log", STACK_LOG, PRIO_LOG, TN_LOG);
  h_long = create(long_task, "long", STACK_LONG, PRIO_LONG, TN_LONG);
  (void)h_imu;
  (void)h_servo;
  (void)h_traj;
  vTaskStartScheduler(); /* сюди повертається лише при нестачі heap для idle */
  for (;;) {
  }
}
