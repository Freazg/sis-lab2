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
2. **Bring-up інтерфейсів на рівні STM32 HAL** з компонентами рівно з пулу методички для форми B: UART (UART Logger), I²C (MPU-6050, LCD1602), ADC (Potentiometer), GPIO (Push button). Кожна `MX_*_Init()` звітує `HAL_OK`, кожен I²C-пристрій відповідає на скан, MPU-6050 повертає `WHO_AM_I = 0x68`.
3. **Розуміння апаратної частини.** Які піни і alternate functions має обраний MCU, чим відрізняються від F4, що реально вміє симулятор.
4. **Сформульований продукт.** PRD для Motion-Mirroring Teleoperator (тема 6) з обладнанням з Додатку A, вимогами, які можна виміряти, і планом тестування.

Ключове уточнення: NUCLEO-F411RE/F446RE **не симулюються у Wokwi**, тому bring-up виконано на єдиній Nucleo-64 у Wokwi — NUCLEO-C031C6. Пояснення — в [hardware-components.md](hardware-components.md).

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
| Компоненти у Wokwi | MPU-6050 (I²C), LCD1602 (I²C), Potentiometer (ADC), Push button (GPIO), UART Logger, Logic Analyzer — усі з пулу п. 2.2 методички |
| Використання пам'яті | Flash 26 032 B / 32 KB (79 %), RAM 2 040 B / 12 KB (17 %), `-Os` |

### 3.2 Кроки

**Крок 1 — build-система** ([a1b15d8](https://github.com/USER/REPO/commit/a1b15d8)).
`docker/Dockerfile` — дослівно з методички. Кореневий `Makefile` запускає `make -C firmware all` у контейнері з `--user $(id -u):$(id -g)`. Додано цілі `image`, `sim` (Wokwi CLI), `flash`, `flash-hw`, `monitor`, `clean`.

**Крок 2 — скелет проєкту** ([f5fd68e](https://github.com/USER/REPO/commit/f5fd68e), [0c65043](https://github.com/USER/REPO/commit/0c65043)).
Структура повторює CubeMX: `Core/{Inc,Src}`, `Drivers/{CMSIS,STM32C0xx_HAL_Driver}`, `startup_stm32c031xx.s`, лінкер-скрипт `STM32C031C6Tx_FLASH.ld`, GNU `Makefile` з `-mcpu=cortex-m0plus -mthumb`. Драйвери взято з офіційних репозиторіїв ST на GitHub. У `stm32c0xx_hal_conf.h` увімкнено лише потрібні модулі.
<!-- TODO: створити в CubeMX проєкт NUCLEO-C031C6 з тими ж пінами (таблиця в hardware-components.md), зберегти firmware/firmware.ioc і зробити 2 скріншоти: -->
![CubeMX Pinout](screenshots/cubemx-pinout.png)
![CubeMX Clock Configuration](screenshots/cubemx-clock.png)

**Крок 3 — «прошивка» в Wokwi** ([56e146d](https://github.com/USER/REPO/commit/56e146d), [2517cc1](https://github.com/USER/REPO/commit/2517cc1)).
`wokwi.toml` вказує на `firmware/build/firmware.{hex,elf}`. `diagram.json` містить плату і лише компоненти з пулу методички. UART Logger (`$serialMonitor`) підключено до PA2/PA3 (USART2 = ST-Link VCP на Nucleo).

**Крок 4 — перша відповідь по UART** ([2f69063](https://github.com/USER/REPO/commit/2f69063)).
`_write()` у `Core/Src/syscalls.c` перенаправляє `printf` у `HAL_UART_Transmit(&huart2, …)`. Після запуску в Serial Monitor видно банер і рядки `tick 1`, `tick 2`, `tick 3` щосекунди.
<!-- TODO: скріншот Serial Monitor після першого запуску -->
![Wokwi Serial Monitor](screenshots/wokwi-uart.png)

**Крок 5 — дослідження компонентів** ([dd98834](https://github.com/USER/REPO/commit/dd98834), [2517cc1](https://github.com/USER/REPO/commit/2517cc1)).
[hardware-components.md](hardware-components.md): відповідність кожного компонента пулу методички, інтерфейс, адреса, живлення, регістри ідентифікації, струм — з посиланнями на розділи datasheet. Окремо — таблиця пінів з alternate functions.

**Крок 6 — ініціалізація інтерфейсів** ([2f69063](https://github.com/USER/REPO/commit/2f69063), [2517cc1](https://github.com/USER/REPO/commit/2517cc1)).
Кожна `MX_*_Init()` повертає `HAL_StatusTypeDef`, `main()` друкує `[ OK ]` або `[FAIL] → HAL_ERROR/BUSY/TIMEOUT`. Перевірка, що шини «живі»:
- I²C: скан `HAL_I2C_IsDeviceReady()` по 0x08–0x77 → `0x27` (LCD1602) і `0x68` (MPU-6050). Далі `WHO_AM_I` (0x75) = `0x68`, ініціалізація HD44780 через PCF8574, текст на LCD.
- ADC: значення потенціометра 0…4095 у лозі й на LCD.
- GPIO: натискання кнопки → `[B1] HOLD` / `[B1] LIVE`, LED LD4 горить у HOLD.
- Логічний аналізатор (SCL, SDA, UART_TX, BTN): пачки тактів SCL під час скану на старті, далі транзакції до 0x68 і 0x27 кожні 200 мс; кадри UART щосекунди.
<!-- TODO: Stop simulation → lr1-capture.vcd → відкрити в PulseView → скріншот SCL/SDA -->
![Logic analyzer capture](screenshots/logic-i2c.png)

Очікуваний вивід (замініть скріншотом реального запуску):

```
==== LR1 bring-up: NUCLEO-C031C6 (STM32C031C6, Cortex-M0+) ====
HAL v1.x.x, SYSCLK = 48000000 Hz
[ OK ] SystemClock 48MHz
[ OK ] GPIO (LED, B1)
[ OK ] USART2 115200 8N1
[ OK ] I2C1 100 kHz
[ OK ] ADC1 IN0 12-bit
I2C1 scan: 0x27 0x68  (2 device(s))
MPU-6050: WHO_AM_I = 0x68 (match)
[ OK ] LCD1602 @0x27 init
Ready. B1 -> LIVE/HOLD.

tick 1 | t=... ms | pot=2048 | acc=     0      0  16384 | pitch=  +0 roll=  +0 | LIVE
tick 2 | ...
```

**Крок 7 — тема і PRD** ([033993a](https://github.com/USER/REPO/commit/033993a), [2517cc1](https://github.com/USER/REPO/commit/2517cc1)).
Обрано тему №6 Motion-Mirroring Teleoperator. Обладнання в PRD — рівно зі списку теми в Додатку A. Вимоги вимірювані: латентність ≤ 50 мс, частота fusion 200 Hz. Деталі — [PRD.md](PRD.md): 11 функціональних вимог, обладнання з альтернативами, план тестування для кожної FR.

**Крок 8 — репозиторій.** Приватний репозиторій, робота в гілці `lab1`, здача через pull request у `main`.

### 3.3 Журнал

| Дата | Що зроблено |
|---|---|
| 08.10.2026 | Прочитано методичку. Встановлено, що Wokwi не підтримує F4 → обрано NUCLEO-C031C6. Зібрано Docker-образ, завантажено HAL/CMSIS для C0 |
| 08.10.2026 | Написано ініціалізацію периферії, звірено AF пінів, перша успішна збірка. Перевірено відтворюваність збірки |
| 08.10.2026 | Схема Wokwi, документація компонентів, PRD |
| 09.10.2026 | Набір компонентів у Wokwi приведено точно до пулу методички: прибрано серво і SPI-флеш, додано LCD1602 (I²C) |
| [дата] | Запуск у Wokwi, скріншоти, захоплення логічним аналізатором, відео |

### 3.4 Проблеми та їх вирішення

#### Проблема 1: обрана плата не запускається в симуляторі
- **Симптом.** Для форми A методичка дає NUCLEO-F411RE/F446RE, але в бібліотеці частин Wokwi таких плат немає.
- **Причина.** Wokwi симулює лише три STM32: C031C6, L031K6 і F103C8 (Blue Pill). Ядра Cortex-M4 серед них немає.
- **Рішення.** Обрано NUCLEO-C031C6 — єдину Nucleo-64 у Wokwi: ST-Link VCP на USART2 (PA2/PA3), LED на PA5, кнопка на PC13. Тулчейн перенастроєно на `-mcpu=cortex-m0plus` без FPU, підключено STM32C0 HAL ([f5fd68e](https://github.com/USER/REPO/commit/f5fd68e), [0c65043](https://github.com/USER/REPO/commit/0c65043)).

#### Проблема 2: у схемі були компоненти поза пулом форми B
- **Симптом.** Перша версія схеми і прошивки містила 2 серво (TIM3 PWM) і код для SPI-флеш W25Q64 — з обладнання теми 6. Цих компонентів немає в рекомендованому пулі Wokwi для форми B (п. 2.2).
- **Причина.** Обладнання теми з Додатку A змішали з пулом компонентів форми B.
- **Рішення.** Схему і прошивку зведено до компонентів з пулу, рівно як у прикладі мінімуму з методички: MPU-6050 (I²C) + Potentiometer (ADC) + Push button (GPIO) + LCD1602 (I²C). Додано драйвер LCD1602 через PCF8574. Модулі HAL TIM і SPI вимкнено ([2517cc1](https://github.com/USER/REPO/commit/2517cc1)). Обладнання теми 6 (SG92R, W25Q64, FT232RL) лишилось лише в PRD.

#### Проблема 3: неправильна розводка Arduino-роз'єму в документації
- **Симптом.** У першій версії `hardware-components.md` VCP був позначений як D0/D1, як на більшості F4 Nucleo.
- **Причина.** На NUCLEO-C031C6 D0/D1 — це PB7/PB6 (USART1), а USART2 на PA2/PA3 доступний лише через ST-Link VCP. Виявлено звіркою з `board.json` плати у Wokwi.
- **Рішення.** Таблицю виправлено ([dd98834](https://github.com/USER/REPO/commit/dd98834)). Висновок: не переносити розводку між серіями STM32 «з пам'яті».

#### Проблема 4: прошивка займає майже всю Flash
- **Симптом.** Перша збірка (`-Og`): `FLASH: 29132 B / 32 KB (88.9 %)`. На ЛР2–4 місця не лишилось би.
- **Причина.** У C031 лише 32 KB. Найбільше займають `printf` з newlib-nano (`_vfprintf_r`, `_printf_i`, `__sfvwrite_r` ≈ 2 KB) і програмна float-арифметика, бо Cortex-M0+ не має FPU (`__aeabi_fadd/fsub/fmul/fdiv`, `atanf` ≈ 2.5 KB). Знайдено через `arm-none-eabi-nm --size-sort`.
- **Рішення.** `-Og` → `-Os` ([0c65043](https://github.com/USER/REPO/commit/0c65043)), `-g3` лишився для налагодження. Поточна збірка: 26 032 B (79 %).

#### Проблема 5: Docker встановлений, але образ не збирається
- **Симптом.** `docker --version` працював, а `docker info` (і будь-який `docker build`/`run`) — ні: немає з'єднання з демоном.
- **Причина.** Docker Desktop на macOS встановлений, але не запущений. CLI є, VM з демоном немає.
- **Рішення.** `open -a Docker`, дочекатись, поки `docker info` відпрацює. Перевірка `docker info` додана в README як перший крок.

#### Проблема 6: попередження `-Wformat-truncation` для рядка LCD
- **Симптом.** `warning: '  ' directive output may be truncated writing 2 bytes into a region of size between 1 and 3` у `snprintf` для другого рядка LCD (16 символів).
- **Причина.** Значення ADC зберігалось у `uint16_t`, і компілятор допускав до 5 цифр для `%4u` та до 6 цифр для відсотків. Рядок тоді не вліз би в 16 символів.
- **Рішення.** Явно обмежено діапазон: `raw = pot & 0x0FFF` (12-бітний ADC) і `pct ≤ 100`. Збірка без жодного warning ([2517cc1](https://github.com/USER/REPO/commit/2517cc1)).

#### Проблема 7 (попереджена): `printf` нічого не виводить до заповнення буфера
- **Симптом (відтворюється без фіксу).** Без `setvbuf` рядки в Serial Monitor з'являються пачками або не з'являються зовсім.
- **Причина.** З `-specs=nosys.specs` функція `_fstat()` повертає помилку. newlib тоді не вважає stdout терміналом і вмикає повну буферизацію (BUFSIZ) замість построкової.
- **Рішення.** `setvbuf(stdout, NULL, _IONBF, 0)` одразу після `MX_USART2_UART_Init()` ([2f69063](https://github.com/USER/REPO/commit/2f69063)). Перевірка: закоментувати рядок і порівняти вивід у Wokwi.

#### Проблема 8 (попереджена): артефакти збірки належать root
- **Причина.** Контейнер за замовчуванням працює від root, тож `firmware/build/` на хості не видаляється без `sudo`.
- **Рішення.** `--user $(id -u):$(id -g)` у `DOCKER_RUN` ([a1b15d8](https://github.com/USER/REPO/commit/a1b15d8)). Перевірено: `ls -la firmware/build` показує власника — поточного користувача.

<!-- TODO: допишіть проблеми, на які натрапите під час запуску в Wokwi / CubeMX -->

## 4. Висновок

Що вивчено конкретно:
- Як HAL будує дерево тактування: `HAL_RCC_OscConfig()` вмикає HSI48 і дільник HSIDIV, `HAL_RCC_ClockConfig()` перемикає SYSCLK. Для 48 MHz перед цим треба виставити 1 wait state flash, інакше CPU читатиме сміття.
- Як працює MSP-шар: `HAL_xxx_Init()` викликає `HAL_xxx_MspInit()`, де вмикається тактування периферії і налаштовуються піни з AF.
- Як розрахувати I²C `TIMINGR` (PRESC/SCLL/SCLH/SDADEL/SCLDEL) для 100 kHz при 48 MHz.
- Як два пристрої ділять одну I²C-шину (MPU-6050 0x68 і LCD1602 0x27) і як HD44780 керується через 8-бітний розширювач PCF8574 у 4-бітному режимі.
- Що симулятор має обмеження: немає STM32F4, немає DMA, частково RCC. Прошивку треба писати так, щоб вона повідомляла про відсутній пристрій, а не зависала.

Що далі (ЛР2): драйвер MPU-6050 з калібруванням зміщень, таймер на 200 Hz для fusion, комплементарний фільтр (FR-01, FR-02, FR-05).

Що зробив би інакше: одразу звірив би кожен компонент зі списком дозволених для обраної форми, перевірив би підтримку плати в симуляторі і таблицю AF для конкретного MCU, а не переносив досвід з F4.

## 5. Матеріали та посилання

1. STMicroelectronics. *DS13867: STM32C031x4/x6 datasheet.* https://www.st.com/resource/en/datasheet/stm32c031c6.pdf
2. STMicroelectronics. *RM0490: STM32C0x1 reference manual.* https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
3. STMicroelectronics. *UM2953: STM32 Nucleo-64 board (MB1717).* https://www.st.com/resource/en/user_manual/um2953-stm32-nucleo64-board-mb1717-stmicroelectronics.pdf
4. STMicroelectronics. *stm32c0xx-hal-driver.* https://github.com/STMicroelectronics/stm32c0xx-hal-driver
5. STMicroelectronics. *cmsis-device-c0.* https://github.com/STMicroelectronics/cmsis-device-c0
6. STMicroelectronics. *cmsis-core (CMSIS 6).* https://github.com/STMicroelectronics/cmsis-core
7. InvenSense. *MPU-6000/6050 Product Specification rev 3.4.* https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf
8. InvenSense. *MPU-6000/6050 Register Map rev 4.2.* https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf
9. NXP. *PCF8574; PCF8574A — Remote 8-bit I/O expander for I²C-bus.* https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf
10. Wokwi Docs. *Supported Hardware.* https://docs.wokwi.com/getting-started/supported-hardware
11. Wokwi Docs. *STM32 Nucleo64 C031C6.* https://docs.wokwi.com/parts/board-st-nucleo-c031c6
12. Wokwi. *wokwi-boards: st-nucleo-c031c6/board.json* (розводка пінів). https://github.com/wokwi/wokwi-boards/tree/main/boards/st-nucleo-c031c6
13. stm32duino. *PeripheralPins.c для STM32C031C(4-6)* (зведена таблиця AF). https://github.com/stm32duino/Arduino_Core_STM32/tree/main/variants/STM32C0xx
14. Hitachi. *HD44780U (LCD-II) datasheet.* https://www.sparkfun.com/datasheets/LCD/HD44780.pdf
15. Wokwi Docs. *wokwi-lcd1602.* https://docs.wokwi.com/parts/wokwi-lcd1602

## 6. Відеозвіт

<!-- TODO: 2–3 хв запису екрана: представитись (ім'я, курс, група), make build, Wokwi Start Simulator,
     показати лог і LCD, покрутити потенціометр, змінити прискорення MPU-6050, натиснути кнопку (HOLD). Залити на Google Drive. -->
Посилання: [Google Drive — відеозвіт ЛР1](https://drive.google.com/…)

## 7. PRD

Див. [docs/PRD.md](PRD.md).
