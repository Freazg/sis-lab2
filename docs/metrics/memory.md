# Використання пам'яті

Генерується `make mem` (tools/mem_report.py). Збірка Wokwi, `-Os`, newlib-nano, власний форматер рядків.

## Регіони (`-Wl,--print-memory-usage`)

```
Memory region   Used Size  Region Size  %age Used
          RAM:     3848 B       12 KB     31.32%
        FLASH:    31484 B       32 KB     96.08%
```

`arm-none-eabi-size`: text=31364, data=120, bss=3736.
У RAM входять резерви лінкер-скрипта: стек 1024 Б і heap 512 Б. Heap не використовується: символів malloc/_sbrk в образі немає.

## Секції (`size -A`)

```
/workspace/firmware/build-mem/firmware.elf  :
section                size        addr
.isr_vector             192   134217728
.text                 26908   134217920
.rodata                4256   134244828
.ARM                      8   134249084
.init_array               4   134249092
.fini_array               4   134249096
.data                   112   536870912
.bss                   2196   536871024
._user_heap_stack      1540   536873220
.ARM.attributes          40           0
.comment                 51           0
.debug_info          188890           0
.debug_abbrev         25114           0
.debug_loc           112957           0
.debug_aranges         5672           0
.debug_ranges          9856           0
.debug_macro          88138           0
.debug_line          131847           0
.debug_str           549144           0
.debug_frame          16328           0
Total               1163257
```

## Флеш за шарами

| Шар | Байт | Частка |
|---|---:|---:|
| App | 9880 | 35.3 % |
| Drivers | 8934 | 31.9 % |
| libc | 3740 | 13.4 % |
| Core | 1878 | 6.7 % |
| Platform | 1814 | 6.5 % |
| Devices | 1730 | 6.2 % |

## Модулі (символи ELF)

| Модуль | Флеш, Б | RAM, Б |
|---|---:|---:|
| libc/libgcc | 3740 | 125 |
| App/app.c | 2110 | 0 |
| Drivers/stm32c0xx_hal_i2c.c | 2084 | 0 |
| Platform/platform.c | 1814 | 109 |
| Drivers/stm32c0xx_hal_spi.c | 1678 | 0 |
| App/app_traj.c | 1448 | 0 |
| Drivers/stm32c0xx_hal_tim.c | 1422 | 0 |
| App/selftest.c | 1416 | 0 |
| Drivers/stm32c0xx_hal_uart.c | 1394 | 0 |
| App/app_cmd.c | 1344 | 0 |
| Drivers/stm32c0xx_hal_rcc.c | 1124 | 0 |
| Core/main.c | 1068 | 1488 |
| App/app_cap.c | 952 | 0 |
| App/cli.c | 886 | 0 |
| Core/stm32c0xx_hal_msp.c | 668 | 0 |
| Drivers/stm32c0xx_hal_gpio.c | 636 | 0 |
| App/app_io.c | 610 | 0 |
| Devices/w25q64.c | 562 | 0 |
| Devices/mpu6050.c | 552 | 0 |
| App/traj.c | 454 | 0 |
| Devices/imath.c | 440 | 0 |
| App/motion.c | 304 | 0 |
| App/perf.c | 270 | 0 |
| Drivers/stm32c0xx_hal.c | 192 | 9 |

## Найбільші символи коду

| Символ | Байт |
|---|---:|
| `selftest_run` | 1112 |
| `main` | 1052 |
| `app_cap` | 952 |
| `app_execute` | 784 |
| `HAL_RCC_OscConfig` | 624 |
| `HAL_SPI_TransmitReceive` | 564 |
| `imu_task` | 548 |
| `__divsi3` | 460 |
| `__divdi3` | 460 |
| `HAL_I2C_Mem_Read` | 452 |
| `cli_parse` | 428 |
| `__udivmoddi4` | 406 |
| `HAL_SPI_Transmit` | 404 |
| `UART_SetConfig` | 380 |
| `HAL_GPIO_Init` | 360 |

## Найбільші змінні в RAM

| Символ | Байт |
|---|---:|
| `app` | 1288 |
| `huart2` | 148 |
| `huart1` | 148 |
| `hw` | 120 |
| `hspi1` | 100 |
| `impure_data` | 96 |
| `hi2c1` | 84 |
| `htim3` | 76 |
| `con_rx_buf` | 64 |
| `eol_t` | 32 |

## Статична оцінка стека

Найглибший ланцюжок від `main`: **1020 Б**

`main → app_poll → run_task → cli_task → app_execute → selftest_run → check_servo.constprop.0 → verdict → app_print → platform_i2c_recover → HAL_I2C_Init → HAL_I2C_MspInit → HAL_GPIO_Init`

| Обробник | Глибина з кадром винятку, Б | Ланцюжок |
|---|---:|---|
| SysTick_Handler | 40 | SysTick_Handler |
| EXTI4_15_IRQHandler | 64 | EXTI4_15_IRQHandler → platform_btn_isr → platform_cycles |
| USART2_IRQHandler | 72 | USART2_IRQHandler → platform_con_rx_isr → platform_cycles |

Найгірший випадок із вкладеними перериваннями: 1020 + 176 = **1196 Б** при резерві 1024 Б.

| Найбільші кадри | Байт |
|---|---:|
| `selftest_run` | 328 |
| `app_print` | 180 |
| `app_print_aux` | 180 |
| `app_cap` | 128 |
| `cli_parse` | 112 |
| `imu_task` | 112 |
| `main` | 88 |
| `check_servo.constprop.0` | 80 |
| `print_jitter` | 64 |
| `app_print_stats` | 64 |
| `app_init` | 64 |
| `app_vformat` | 64 |
