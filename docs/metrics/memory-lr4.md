# Пам'ять прошивки на FreeRTOS (ЛР4)

Генерується `tools/mem_report_lr4.py`. Збірка Wokwi, `-Os`, LTO для власного коду і HAL, ядро без LTO.

## Регіони

| Регіон | ЛР4 | Ліміт |
|---|---:|---:|
| FLASH | 32620 Б | 32 768 Б |
| RAM | 12224 Б | 12 288 Б |

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
| Rtos | 6540 |
| App | 4640 |
| libc/libgcc | 3618 |
| Core | 2652 |
| Platform | 2128 |
| Devices | 1364 |

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

## Порівняння з ЛР3 (той самий тулчейн, той самий день)

| Збірка | Флеш, Б | RAM, Б |
|---|---:|---:|
| ЛР3, тег lab3, як у тегу (без LTO) | 31476 | 3848 |
| ЛР3, тег lab3, з LTO | 28472 | 3840 |
| ЛР4 | 32620 | 12224 |

## Секції

```
/workspace/firmware/build-mem/firmware.elf  :
section                size        addr
.isr_vector             192   134217728
.text                 27536   134217920
.rodata                4708   134245456
.ARM                      8   134250164
.init_array               4   134250172
.fini_array               4   134250176
.data                   168   536870912
.bss                  11032   536871080
._user_heap_stack      1024   536882112
.ARM.attributes          40           0
.comment                 51           0
.debug_info          144793           0
.debug_abbrev         16376           0
.debug_loc            62776           0
.debug_aranges         1736           0
.debug_ranges          9112           0
.debug_line           68199           0
.debug_str           606873           0
.debug_frame           7004           0
.debug_macro         102135           0
Total               1063771
```
