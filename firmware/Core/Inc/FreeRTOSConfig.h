/**
 * @file    FreeRTOSConfig.h
 * @brief   Конфігурація ядра FreeRTOS V11.3.1 (порт GCC/ARM_CM0, heap_4) для STM32C031C6.
 *          Кожне значення, змінене відносно типового (FreeRTOS/Demo, «Customisation»),
 *          пояснено поруч; зведення — docs/rtos-design.md.
 *
 * Варіанти збірки для сценаріїв методички ЛР4 §7.2 задаються з командного рядка make:
 *   RTOS_TICK_HZ      — частота тіку (§7.2.6), типово 1000
 *   RTOS_HEAP_BYTES   — розмір heap ОС (§7.2.5)
 *   RTOS_STACK_CHECK  — configCHECK_FOR_STACK_OVERFLOW (§7.2.2), типово 2
 *   RTOS_STACK_IMU    — стек задачі imu у словах (§7.2.2), Rtos/rtos_app.c
 *   RTOS_FAULTS=1     — команди fault invmutex|invsem|starve|qflood (§7.2.1, 7.2.3, 7.2.4): у продуктову
 *                       збірку не входять, бо разом з ядром не вміщаються у 32 КБ флеш
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* Лічильник часу для run-time stats і trace: мкс від скидання (TIM14, Platform/platform.c).
 * DWT CYCCNT на Cortex-M0+ немає (ARMv6-M), тому джерело — апаратний таймер. */
extern uint32_t platform_micros(void);
extern void rtos_assert_failed(int line);
extern void rtos_trace_switched_in(unsigned task_number);

#ifndef RTOS_FAULTS
#define RTOS_FAULTS                             0
#endif

/* ---- Планувальник -------------------------------------------------------- */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0   /* на Cortex-M0+ немає CLZ */
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      48000000UL
#ifndef RTOS_TICK_HZ
#define RTOS_TICK_HZ                            1000
#endif
#define configTICK_RATE_HZ                      RTOS_TICK_HZ
#define configMAX_PRIORITIES                    7   /* 0 idle … 6 btn; більше рівнів — більше списків у RAM */
#define configMINIMAL_STACK_SIZE                64  /* слів; стек idle, виміряно командою stack */
#define configMAX_TASK_NAME_LEN                 8
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configENABLE_MPU                        0

/* ---- Механізми взаємодії ------------------------------------------------- */
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configUSE_STREAM_BUFFERS                0   /* прийом UART: кільцевий буфер ISR + сповіщення задачі */
#define configUSE_EVENT_GROUPS                  0
#define configUSE_TIMERS                        0   /* програмні таймери не потрібні: усі періоди — задачі */
#define configUSE_CO_ROUTINES                   0

/* ---- Пам'ять -------------------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#ifndef RTOS_HEAP_BYTES
#define RTOS_HEAP_BYTES                         7168
#endif
#define configTOTAL_HEAP_SIZE                   RTOS_HEAP_BYTES
#define configAPPLICATION_ALLOCATED_HEAP        0

/* ---- Хуки і діагностика (§4.11) ------------------------------------------ */
#ifndef RTOS_STACK_CHECK
#define RTOS_STACK_CHECK                        2
#endif
#define configCHECK_FOR_STACK_OVERFLOW          RTOS_STACK_CHECK
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1   /* вікно завантаження CPU, 1 с */
#define configGENERATE_RUN_TIME_STATS           1
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1   /* snprintf — власний, Rtos/rtos_app.c */
#define configRUN_TIME_COUNTER_TYPE             uint32_t /* мкс: переповнення через 71 хв */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()    /* TIM14 запущено в HAL_Init() */
#define portGET_RUN_TIME_COUNTER_VALUE()        platform_micros()
#define configASSERT(x)                         do { if ((x) == 0) { rtos_assert_failed(__LINE__); } } while (0)

/* Хронологія задач (§6.5): номер задачі і мітка часу в кільцевий буфер */
#define traceTASK_SWITCHED_IN()                 rtos_trace_switched_in((unsigned)pxCurrentTCB->uxTaskNumber)

/* ---- Переривання ----------------------------------------------------------- */
/* Порт ARM_CM0: у Cortex-M0+ немає BASEPRI, критична секція — cpsid i, тобто маскуються всі
 * переривання. configMAX_SYSCALL_INTERRUPT_PRIORITY у цьому порту не використовується; API ядра
 * з суфіксом FromISR можна кликати з будь-якого ISR. SysTick і PendSV — найнижчий пріоритет (3). */
#define configKERNEL_INTERRUPT_PRIORITY         3

/* ---- Набір API ------------------------------------------------------------ */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskSuspend                    1   /* portMAX_DELAY = чекати без timeout */
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskDelete                     0   /* задачі створюються лише на старті */
#define INCLUDE_xTaskGetCurrentTaskHandle       0

#endif /* FREERTOS_CONFIG_H */
