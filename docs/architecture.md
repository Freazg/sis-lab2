# Архітектура прошивки ЛР2

![Component diagram](lab_2/img/component_diagram.svg)

## Шари і правила залежностей

| Шар | Каталог | Що містить | Може включати | Не може включати |
|---|---|---|---|---|
| Згенерований / вендорський код | `firmware/Core/`, `firmware/Drivers/` | `main.c` (composition root), MSP, обробники IRQ, HAL/CMSIS ST | усе | — |
| Адаптація до плати | `firmware/Platform/` | `board.h` (піни, AF, хендли, параметри шин), `platform.c` (реалізації `bus.h` над HAL, UART, GPIO) | HAL, CMSIS, `main.h`, `Devices/` (лише інтерфейси) | `App/` |
| Драйвери пристроїв | `firmware/Devices/` | `mpu6050`, `w25q64`, `servo`, `imath`, `bus.h`, `dev_status` | лише `Devices/` і стандартну бібліотеку C | HAL, `Platform/`, `App/`, `main.h` |
| Прикладна логіка | `firmware/App/` | режими LIVE/HOLD, self-test, CLI, команди `cap`, орієнтація | `App/`, `Devices/`, стандартну бібліотеку C | HAL, `Platform/`, `main.h` |
| Тести | `tests/` | Unity, test doubles, тестові вектори з записів | `Devices/`, `App/`, `doubles/` | HAL, `Platform/`, `Core/` |

Напрямок залежностей: `Core → App → Devices ← Platform → Drivers`. Залежність `Platform → Devices` іде лише на інтерфейси `bus.h`: Platform *реалізує* те, чого потребують драйвери (інверсія залежностей).

## Як правило перевіряється формально

1. **`tools/check_deps.py`** розбирає `#include` в усіх файлах `App/`, `Devices/`, `Platform/` і порівнює зі списком дозволених. Скрипт запускається першим кроком `make build` (ціль `check-deps`) і в CI. Будь-яке порушення дає ненульовий код виходу і ламає збірку. Перевірено вручну: доданий `#include "stm32c0xx_hal.h"` в `App/` дає `ПОРУШЕННЯ ... прикладний шар не знає про HAL`.
2. **Компілятор.** Хостові тести збирають `App/` і `Devices/` з `-I` лише на ці каталоги, без шляхів HAL. Заборонений `#include` у цих шарах — це помилка компіляції `make test`.

## Як драйвер отримує доступ до шини

Драйвер отримує вказівник на структуру з вказівниками на функції та непрозорим контекстом:

```c
typedef struct i2c_bus {
  dev_status_t (*write)(void *ctx, uint8_t addr7, const uint8_t *data, size_t len);
  dev_status_t (*write_read)(void *ctx, uint8_t addr7, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen);
  dev_status_t (*probe)(void *ctx, uint8_t addr7);
  void *ctx;
} i2c_bus_t;
```

- На платі `Platform/platform.c` надає `platform_i2c1` (поверх `HAL_I2C_*` або регістрів I2C), `platform_spi1_flash` (SPI1 + програмний CS# на PB0), `platform_pwm_pan/tilt` (TIM3 CCR1/CCR2).
- У тестах ті самі драйвери отримують `fake_i2c_bus()` (робоча модель регістрів MPU-6050), `mock_spi_bus()` (журнал транзакцій) або `fake_spiflash_bus()`.
- `mpu6050.c` не знає про `HAL_I2C_Mem_Read` і про те, що в симуляторі repeated START замінено на STOP+START (аномалія A2). Ця різниця живе лише в Platform і перемикається `BOARD_TARGET=wokwi|nucleo`.

Так само влаштовані драйвери ST для MEMS (`STMicroelectronics/*-pid`): `stmdev_ctx_t` з `write_reg`/`read_reg` і `handle`. Відмінність: у нас інтерфейс — на рівні транзакції шини, а не регістра, бо той самий `spi_bus_t` має обслуговувати флеш з довільною довжиною команд.

## Де живе знання про плату

Тільки в `Platform/board.h`: піни, AF, `TIMINGR`, дільники SPI/TIM, швидкість UART, оголошення хендлів (визначені в `board.c`). `Core/Inc/main.h` включає `board.h` у секції `USER CODE BEGIN Includes`. MSP-функції беруть піни звідти ж. Таблиця [`docs/pinout.md`](pinout.md) відповідає `board.h` рядок у рядок. Причина: коли пін змінюється в одному місці, не лишається розбіжностей між схемою, MSP і драйвером.

## Код у стилі STM32CubeMX і секції USER CODE

`Core/Src/main.c` і `stm32c0xx_it.c` мають структуру файлів CubeMX: `MX_*_Init()` поза маркерами, власний код — лише між `/* USER CODE BEGIN x */ … /* USER CODE END x */`. Повторна генерація з `.ioc` перезаписує все поза маркерами. Тому виклики `app_init()`/`app_poll()`, збирання `app_hw_t` і обробник `USART2_IRQHandler` стоять усередині секцій. Обмеження секцій:
- нові функції можна додавати лише в `USER CODE BEGIN 0/4`;
- змінені параметри `MX_*_Init()` CubeMX перезапише з `.ioc`, тож їх треба змінювати в `.ioc`, а не в коді;
- генератор не знає про `board.h`, тому номери пінів у `.ioc` треба тримати узгодженими з `board.h` (див. журнал проблем звіту: `.ioc` у формі B не створювався, бо CubeMX вимагає облікового запису myST).

## Збірка

- `make build`: Docker, `check-deps`, власний код із `-Wall -Wextra -Werror -Wshadow -Wformat=2`. Вендорський код — `-Wall -Wextra -Wno-unused-parameter`: попередження придушено лише цілеспрямовано і лише для `Drivers/` і `system_stm32c0xx.c`, у `firmware/Makefile` це окремі правила `own/` і `vendor/`.
- Версії вендорського коду зафіксовані в [`firmware/Drivers/VERSIONS.md`](../firmware/Drivers/VERSIONS.md).
- Відтворюваність: `-ffile-prefix-map`, однаковий образ Docker. CI перевіряє, що дві чисті збірки дають однаковий SHA-256.
