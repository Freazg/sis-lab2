<!--
  Титульний аркуш. Перед експортом у PDF заповніть поля в [квадратних дужках]
  і замініть USER/REPO у посиланнях на коміти:  sed -i '' 's#USER/REPO#<ваш-нік>/<репо>#g' docs/report.md
-->

<div align="center">

Міністерство освіти і науки України
[Назва університету]
[Назва факультету / кафедри]

<br><br>

**ЗВІТ**
з лабораторної роботи №1
з дисципліни «Системне програмування вбудованих систем»

**«Налаштування середовища розробки та bring-up STM32-платформи»**

<br><br>

Виконав: студент групи [група]
**Євгеній Сас**

Перевірив: [посада, ПІБ викладача]

<br>

[Місто] — 2026

</div>

---

## 1. Мета лабораторної

На старті мета виглядала так: «поставити тулчейн і побачити `hello` по UART». Наприкінці вона уточнилась до чотирьох конкретних результатів:

1. **Відтворюване середовище збірки.** Прошивка збирається однією командою `make build` у Docker-образі, однаковому на будь-якій машині. Дві чисті збірки дають побайтно ідентичний `firmware.bin` (той самий SHA-256).
2. **Bring-up усіх інтерфейсів, потрібних проєкту, на рівні STM32 HAL.** UART, I²C, ADC, PWM (TIM), SPI, GPIO. Кожна `MX_*_Init()` звітує `HAL_OK`, а кожен підключений пристрій відповідає (I²C-скан, `WHO_AM_I`).
3. **Розуміння апаратної частини.** Які піни і alternate functions має обраний MCU, чим відрізняються від F4, що реально вміє симулятор.
4. **Сформульований продукт.** PRD для Motion-Mirroring Teleoperator з вимогами, які можна виміряти, і планом тестування.

Ключове уточнення: обрана в методичці NUCLEO-F411RE **не симулюється у Wokwi**, тому роботу виконано на NUCLEO-C031C6. Перенесення на F4 описано в [hardware-components.md](hardware-components.md).

## 2. Розподіл обов'язків

Робота індивідуальна, таблиця не потрібна (п. 4.2 методички).

## 3. Процес виконання

### 3.1 Конфігурація

| Що | Значення |
|---|---|
| Форма | B — Wokwi for VS Code |
| Плата / MCU | NUCLEO-C031C6 / STM32C031C6T6, Cortex-M0+, 48 MHz, 32 KB Flash, 12 KB RAM |
| Тулчейн | Docker `ubuntu:22.04` + `gcc-arm-none-eabi` 10.3 + newlib-nano + srecord |
| HAL | STM32CubeC0 HAL (ST GitHub, `stm32c0xx-hal-driver` @ `a6c02b4`) |
| Компоненти у Wokwi | MPU-6050 (I²C), потенціометр (ADC), кнопка (GPIO), 2× серво (PWM), логічний аналізатор |
| Використання пам'яті | Flash 27 276 B / 32 KB (83 %), RAM 2 208 B / 12 KB (18 %), `-Os` |

### 3.2 Кроки

**Крок 1 — build-система** ([a1b15d8](https://github.com/USER/REPO/commit/a1b15d8)).
`docker/Dockerfile` — дослівно з методички. Кореневий `Makefile` запускає `make -C firmware all` у контейнері з `--user $(id -u):$(id -g)`. Додано цілі `image`, `sim` (Wokwi CLI), `flash`, `flash-hw`, `monitor`, `clean`.

**Крок 2 — скелет проєкту** ([f5fd68e](https://github.com/USER/REPO/commit/f5fd68e), [0c65043](https://github.com/USER/REPO/commit/0c65043)).
Структура повторює CubeMX: `Core/{Inc,Src}`, `Drivers/{CMSIS,STM32C0xx_HAL_Driver}`, `startup_stm32c031xx.s`, лінкер-скрипт `STM32C031C6Tx_FLASH.ld`, GNU `Makefile` з `-mcpu=cortex-m0plus -mthumb`. Драйвери взято з офіційних репозиторіїв ST на GitHub. У `stm32c0xx_hal_conf.h` увімкнено лише потрібні модулі.
<!-- TODO: створити в CubeMX проєкт NUCLEO-C031C6 з тими ж пінами (таблиця в hardware-components.md), зберегти firmware/firmware.ioc і зробити 2 скріншоти: -->
![CubeMX Pinout](screenshots/cubemx-pinout.png)
![CubeMX Clock Configuration](screenshots/cubemx-clock.png)

**Крок 3 — «прошивка» в Wokwi** ([56e146d](https://github.com/USER/REPO/commit/56e146d)).
`wokwi.toml` вказує на `firmware/build/firmware.{hex,elf}`. `diagram.json` містить плату, сенсори і логічний аналізатор. Serial Monitor підключено до PA2/PA3 (USART2 = ST-Link VCP на Nucleo).

**Крок 4 — перша відповідь по UART** ([2f69063](https://github.com/USER/REPO/commit/2f69063)).
`_write()` у `Core/Src/syscalls.c` перенаправляє `printf` у `HAL_UART_Transmit(&huart2, …)`. Після запуску в Serial Monitor видно банер і рядки `tick 1`, `tick 2`, `tick 3` щосекунди.
<!-- TODO: скріншот Serial Monitor після першого запуску -->
![Wokwi Serial Monitor](screenshots/wokwi-uart.png)

**Крок 5 — дослідження компонентів** ([dd98834](https://github.com/USER/REPO/commit/dd98834)).
[hardware-components.md](hardware-components.md): інтерфейс, адреса або режим, живлення, регістри ідентифікації, струм — з посиланнями на розділи datasheet. Окремо — повна таблиця пінів з alternate functions.

**Крок 6 — ініціалізація інтерфейсів** ([2f69063](https://github.com/USER/REPO/commit/2f69063)).
Кожна `MX_*_Init()` повертає `HAL_StatusTypeDef`, `main()` друкує `[ OK ]` або `[FAIL] → HAL_ERROR/BUSY/TIMEOUT`. Перевірка, що шини «живі»:
- I²C: скан `HAL_I2C_IsDeviceReady()` по 0x08–0x77 → `0x68`. Далі `WHO_AM_I` (0x75) = `0x68`.
- SPI: `Read JEDEC ID` (0x9F). У Wokwi SPI-флеш немає → `FF FF FF (not connected)`. На залізі очікується `EF 40 17`.
- PWM: логічний аналізатор на PB4 показує 50 Hz та імпульс 0.544–2.4 мс залежно від потенціометра.
- I²C на аналізаторі: пачки тактів SCL під час скану на старті, далі читання 6 байт акселерометра кожні 20 мс.
<!-- TODO: Stop simulation → lr1-capture.vcd → відкрити в PulseView → скріншот SCL/SDA і PWM -->
![Logic analyzer capture](screenshots/logic-i2c-pwm.png)

Очікуваний вивід (замініть скріншотом реального запуску):

```
==== LR1 bring-up: NUCLEO-C031C6 (STM32C031C6, Cortex-M0+) ====
HAL v1.x.x, SYSCLK = 48000000 Hz
[ OK ] SystemClock 48MHz
[ OK ] GPIO (LED, B1, CS)
[ OK ] USART2 115200 8N1
[ OK ] I2C1 100 kHz
[ OK ] ADC1 IN0 12-bit
[ OK ] TIM3 PWM 50 Hz
[ OK ] SPI1 master mode0
[ OK ] TIM3 CH1 start
[ OK ] TIM3 CH2 start
I2C1 scan: 0x68  (1 device(s))
MPU-6050: WHO_AM_I = 0x68 (match)
W25Q64: JEDEC ID = FF FF FF (not connected)
Ready. Pot -> pan, MPU pitch -> tilt, B1 -> HOLD.

tick 1 | t=... ms | pot=2047 | acc=     0      0  16384 | pan= 90 tilt= 90 | LIVE
tick 2 | ...
```

**Крок 7 — тема і PRD** ([033993a](https://github.com/USER/REPO/commit/033993a)).
Обрано тему №6 Motion-Mirroring Teleoperator. Вона повністю реалізовна у Wokwi (MPU-6050 і серво там є, камера не потрібна) і має вимірювані вимоги: латентність ≤ 50 мс, частота fusion 200 Hz. Деталі — [PRD.md](PRD.md): 12 функціональних вимог, датчики з альтернативами, план тестування для кожної FR.

**Крок 8 — репозиторій.** Приватний репозиторій, робота в гілці `lab1`, здача через pull request у `main`.

### 3.3 Журнал

| Дата | Що зроблено |
|---|---|
| 08.10.2026 | Прочитано методичку. Встановлено, що Wokwi не підтримує F4 → обрано NUCLEO-C031C6. Зібрано Docker-образ, завантажено HAL/CMSIS для C0 |
| 08.10.2026 | Написано ініціалізацію периферії, звірено AF-пінів, перша успішна збірка (Flash 89 % на `-Og` → 83 % на `-Os`). Перевірено відтворюваність збірки |
| 08.10.2026 | Схема Wokwi, документація компонентів, PRD |
| [дата] | Запуск у Wokwi, скріншоти, захоплення логічним аналізатором, відео |

### 3.4 Проблеми та їх вирішення

#### Проблема 1: обрана плата не запускається в симуляторі
- **Симптом.** Методичка рекомендує NUCLEO-F411RE/F446RE, але в бібліотеці частин Wokwi такої плати немає.
- **Причина.** Wokwi симулює лише три STM32: C031C6, L031K6 і F103C8 (Blue Pill). Ядра Cortex-M4 серед них немає.
- **Рішення.** Обрано NUCLEO-C031C6: та сама форма Nucleo-64, ST-Link VCP на USART2 (PA2/PA3), LED на PA5, кнопка на PC13. Тулчейн перенастроєно на `-mcpu=cortex-m0plus` без FPU, підключено STM32C0 HAL ([f5fd68e](https://github.com/USER/REPO/commit/f5fd68e), [0c65043](https://github.com/USER/REPO/commit/0c65043)).

#### Проблема 2: конфлікт пінів TIM3 і SPI1
- **Симптом.** Перший план розводки: серво на PA6/PA7 (TIM3_CH1/CH2), як у прикладах для F4. Але W25Q64 теж потрібні PA6/PA7 як SPI1 MISO/MOSI.
- **Причина.** На STM32C031 PA6/PA7 мультиплексовані між TIM3 (AF1) і SPI1 (AF0), а одночасно пін може мати лише одну AF. Перевірено за таблицею AF (DS13867) і `PeripheralPins.c` з stm32duino.
- **Рішення.** TIM3_CH1/CH2 перенесено на PB4/PB5 (теж AF1, Arduino D5/D6), SPI1 — PB3/PA6/PA7 з AF0 ([2f69063](https://github.com/USER/REPO/commit/2f69063)).

#### Проблема 3: неправильна розводка Arduino-роз'єму в документації
- **Симптом.** У першій версії `hardware-components.md` VCP був позначений як D0/D1, як на більшості F4 Nucleo.
- **Причина.** На NUCLEO-C031C6 D0/D1 — це PB7/PB6 (USART1), а USART2 на PA2/PA3 доступний лише через ST-Link VCP. Виявлено звіркою з `board.json` плати у Wokwi.
- **Рішення.** Таблицю виправлено ([dd98834](https://github.com/USER/REPO/commit/dd98834)). Висновок: не переносити розводку між серіями STM32 «з пам'яті».

#### Проблема 4: прошивка займає майже всю Flash
- **Симптом.** Перша збірка: `FLASH: 29132 B / 32 KB (88.9 %)`. На ЛР2–4 місця не лишилось би.
- **Причина.** У C031 лише 32 KB. Найбільше займають `printf` з newlib-nano (`_vfprintf_r`, `_printf_i`, `__sfvwrite_r` ≈ 2 KB) і програмна float-арифметика, бо Cortex-M0+ не має FPU (`__aeabi_fadd/fsub/fmul/fdiv`, `atanf` ≈ 2.5 KB). Знайдено через `arm-none-eabi-nm --size-sort`.
- **Рішення.** `-Og` → `-Os`: 27 276 B (83 %), `-g3` лишився для налагодження ([0c65043](https://github.com/USER/REPO/commit/0c65043)). На ЛР2 плануємо fixed-point fusion замість float.

#### Проблема 5: Docker встановлений, але образ не збирається
- **Симптом.** `docker --version` працював, а `docker info` (і будь-який `docker build`/`run`) — ні: немає з'єднання з демоном.
- **Причина.** Docker Desktop на macOS встановлений, але не запущений. CLI є, VM з демоном немає.
- **Рішення.** `open -a Docker`, дочекатись, поки `docker info` відпрацює. Перевірка `docker info` додана в README як перший крок.

#### Проблема 6 (попереджена): `printf` нічого не виводить до заповнення буфера
- **Симптом (відтворюється без фіксу).** Без `setvbuf` рядки в Serial Monitor з'являються пачками або не з'являються зовсім.
- **Причина.** З `-specs=nosys.specs` функція `_fstat()` повертає помилку. newlib тоді не вважає stdout терміналом і вмикає повну буферизацію (BUFSIZ) замість построкової.
- **Рішення.** `setvbuf(stdout, NULL, _IONBF, 0)` одразу після `MX_USART2_UART_Init()` ([2f69063](https://github.com/USER/REPO/commit/2f69063)). Перевірка: закоментувати рядок і порівняти вивід у Wokwi.

#### Проблема 7 (попереджена): артефакти збірки належать root
- **Причина.** Контейнер за замовчуванням працює від root, тож `firmware/build/` на хості не видаляється без `sudo`.
- **Рішення.** `--user $(id -u):$(id -g)` у `DOCKER_RUN`. Перевірено: `ls -la firmware/build` показує власника — поточного користувача.

<!-- TODO: допишіть проблеми, на які натрапите під час запуску в Wokwi / CubeMX -->

## 4. Висновок

Що вивчено конкретно:
- Як HAL будує дерево тактування: `HAL_RCC_OscConfig()` вмикає HSI48 і дільник HSIDIV, `HAL_RCC_ClockConfig()` перемикає SYSCLK. Для 48 MHz перед цим треба виставити 1 wait state flash, інакше CPU читатиме сміття.
- Як працює MSP-шар: `HAL_xxx_Init()` викликає `HAL_xxx_MspInit()`, де вмикається тактування периферії і налаштовуються піни з AF. PWM-піни варто налаштовувати після конфігурації каналів (`HAL_TIM_MspPostInit`), щоб серво не смикались.
- Як розрахувати I²C `TIMINGR` (PRESC/SCLL/SCLH/SDADEL/SCLDEL) і TIM3 для серво (PSC → 1 MHz, ARR = 19999, CCR = мкс).
- Що симулятор має обмеження: немає DMA, частково RCC, немає SPI-флеш. Прошивку треба писати так, щоб вона повідомляла про відсутній пристрій, а не зависала.

Що далі (ЛР2): драйвер MPU-6050 з калібруванням зміщень, таймер на 200 Hz для fusion, комплементарний фільтр (FR-01, FR-02, FR-05).

Що зробив би інакше: одразу перевірив би підтримку плати в симуляторі і таблицю AF для конкретного MCU, а не переносив досвід з F4.

## 5. Матеріали та посилання

1. STMicroelectronics. *DS13867: STM32C031x4/x6 datasheet.* https://www.st.com/resource/en/datasheet/stm32c031c6.pdf
2. STMicroelectronics. *RM0490: STM32C0x1 reference manual.* https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
3. STMicroelectronics. *UM2953: STM32 Nucleo-64 board (MB1717).* https://www.st.com/resource/en/user_manual/um2953-stm32-nucleo64-board-mb1717-stmicroelectronics.pdf
4. STMicroelectronics. *stm32c0xx-hal-driver.* https://github.com/STMicroelectronics/stm32c0xx-hal-driver
5. STMicroelectronics. *cmsis-device-c0.* https://github.com/STMicroelectronics/cmsis-device-c0
6. STMicroelectronics. *cmsis-core (CMSIS 6).* https://github.com/STMicroelectronics/cmsis-core
7. InvenSense. *MPU-6000/6050 Product Specification rev 3.4.* https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf
8. InvenSense. *MPU-6000/6050 Register Map rev 4.2.* https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf
9. Winbond. *W25Q64JV datasheet.* https://www.winbond.com/hq/product/code-storage-flash-memory/serial-nor-flash/
10. Wokwi Docs. *Supported Hardware.* https://docs.wokwi.com/getting-started/supported-hardware
11. Wokwi Docs. *STM32 Nucleo64 C031C6.* https://docs.wokwi.com/parts/board-st-nucleo-c031c6
12. Wokwi. *wokwi-boards: st-nucleo-c031c6/board.json* (розводка пінів). https://github.com/wokwi/wokwi-boards/tree/main/boards/st-nucleo-c031c6
13. stm32duino. *PeripheralPins.c для STM32C031C(4-6)* (зведена таблиця AF). https://github.com/stm32duino/Arduino_Core_STM32/tree/main/variants/STM32C0xx

## 6. Відеозвіт

<!-- TODO: 2–3 хв запису екрана: представитись (ім'я, курс, група), make build, Wokwi Start Simulator,
     показати лог, покрутити потенціометр, нахилити MPU-6050, натиснути B1 (HOLD). Залити на Google Drive. -->
Посилання: [Google Drive — відеозвіт ЛР1](https://drive.google.com/…)

## 7. PRD

Див. [docs/PRD.md](PRD.md).
