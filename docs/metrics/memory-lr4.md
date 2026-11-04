# Пам'ять прошивки на FreeRTOS (ЛР4)

Генерується `tools/mem_report_lr4.py`. Збірка Wokwi, `-Os`, LTO для власного коду і HAL, ядро без LTO.

## Регіони

| Регіон | ЛР4 | Ліміт |
|---|---:|---:|
| FLASH | 32724 Б | 32 768 Б |
| RAM | 12136 Б | 12 288 Б |

## Ядро FreeRTOS у флеші і RAM (map-файл, §6.5)

| Об'єкт | Флеш, Б | RAM, Б |
|---|---:|---:|
| `tasks.o` | 4495 | 288 |
| `queue.o` | 1542 | 0 |
| `list.o` | 104 | 0 |
| `heap_4.o` | 636 | 7708 |
| `port.o` | 476 | 4 |
| `portasm.o` | 156 | 0 |
| **разом ядро** | **7409** | **8000** |

`heap_4.o` у RAM — масив `ucHeap` (configTOTAL_HEAP_SIZE = 7680 Б): у ньому стеки задач, TCB і черги.

## Флеш за шарами (символи ELF)

| Шар | Байт |
|---|---:|
| Middlewares | 7320 |
| Rtos | 6702 |
| App | 4640 |
| libc/libgcc | 3618 |
| Core | 2676 |
| Platform | 1998 |
| Devices | 1364 |
| Drivers | 40 |

## Склад heap ОС (§6.4)

| Об'єкт | Байт з заголовком heap_4 |
|---|---:|
| стек btn (112 слів) | 456 |
| стек imu (160 слів) | 648 |
| стек servo (160 слів) | 648 |
| стек cli (320 слів) | 1288 |
| стек traj (192 слів) | 776 |
| стек log (96 слів) | 392 |
| стек long (176 слів) | 712 |
| стек IDLE (64 слова) | 264 |
| TCB × 8 (sizeof(TCB_t) = 88) | 768 |
| черга log: 4 × 97 Б + Queue_t | 480 |
| черга traj: 16 × 6 Б + Queue_t | 184 |
| м'ютекси CON, I2C, FLASH: 3 × Queue_t (80) | 264 |
| **оцінка зайнятого** | **6880** |
| зарезервовано (configTOTAL_HEAP_SIZE) | 7680 |

Виміряно після S3 (команда `heap`): free = 792 Б, min_ever_free = 496 Б, total = 7680 Б. Використано на піку 7184 Б (оцінка вище — 6880 Б; різниця — тимчасовий масив `vTaskGetRunTimeStatistics` у команді `tasks`).

## Порівняння з ЛР3 (той самий тулчейн, той самий день)

| Збірка | Флеш, Б | RAM, Б |
|---|---:|---:|
| ЛР3, тег lab3, як у тегу (без LTO) | 31476 | 3848 |
| ЛР3, тег lab3, з LTO | 28472 | 3840 |
| ЛР4 | 32724 | 12136 |

## Секції

```
/workspace/firmware/build-mem/firmware.elf  :
section                size        addr
.isr_vector             192   134217728
.text                 27632   134217920
.rodata                4716   134245552
.ARM                      8   134250268
.init_array               4   134250276
.fini_array               4   134250280
.data                   168   536870912
.bss                  10944   536871080
._user_heap_stack      1024   536882024
.ARM.attributes          40           0
.comment                 51           0
.debug_info          144483           0
.debug_abbrev         16392           0
.debug_loc            62685           0
.debug_aranges         1736           0
.debug_ranges          9072           0
.debug_line           68396           0
.debug_str           606910           0
.debug_frame           7152           0
.debug_macro         102141           0
Total               1063750
```
