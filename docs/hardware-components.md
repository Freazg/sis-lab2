# Апаратні компоненти (актуалізовано для ЛР2)

> **ЛР2:** до стенда підключено всі пристрої PRD теми 6 одночасно. Повна розводка, адреси, бюджет живлення
> і параметри шин — у [`pinout.md`](pinout.md); тут — склад стенда і характеристики компонентів.

| Компонент (PRD / Додаток A) | Інтерфейс | У Wokwi | Ключові параметри | Джерело |
|---|---|---|---|---|
| NUCLEO-C031C6 (замість F446RE/F411RE — немає в Wokwi) | — | `board-st-nucleo-c031c6` | Cortex-M0+ 48 МГц, 32 КБ Flash, 12 КБ RAM, LQFP48 | DS13867 Rev 4, UM2953 Rev 2 |
| MPU-6050 (GY-521) | I2C1 0x68, Fm | `wokwi-mpu6050` | WHO_AM_I 0x68; ±2 g = 16384 LSB/g; 3.8 мА; VDD 2.375–3.46 В | PS-MPU-6000A-00 Rev 3.1 §6.2–6.3, RM-MPU-6000A-00 Rev 4.0 |
| W25Q64JV | SPI1 mode 0, 3 МГц, CS# PB0 | custom chip `chips/w25q64` | JEDEC EF 40 17; VCC 2.7–3.6 В (не 5 В tolerant); tPP 0.4/3 мс, tSE 45/400 мс; 25 мА при записі | W25Q64JV Rev J §8–9 |
| SG92R ×2 | TIM3 CH1/CH2 PWM 50 Гц | `wokwi-servo` ×2 | 4.8 В, 0.1 с/60°, dead band 1 мкс; імпульс 544…2400 мкс (калібрування) | towerpro.com.tw |
| FT232RL | USART1 115200 8N1 | custom chip `chips/ft232rl` | VCCIO 1.8–5.25 В; 15 мА від USB ПК | FT232R DS v2.10 Table 5.2 |
| LED ×2 (Live / Rec) | GPIO PA15, PB10 | `wokwi-led` + 330 Ω | ≈ 4 мА кожен | DS13867 Table 22 |
| B1 USER | GPIO PC13 | `wokwi-pushbutton` | pull-up, active low | UM2953 §6.11 |
| Логічний аналізатор | — | `wokwi-logic-analyzer` | 8 каналів, VCD 1 нс | docs.wokwi.com |

Нижче — розділи ЛР1 (компоненти пулу форми B), збережені для історії.

---


Форма виконання: **B — симулятор Wokwi**.
Компоненти ЛР1 взято **лише з рекомендованого пулу методички для форми B** (п. 2.2). Набір збігається з прикладом мінімуму з методички: «MPU-6050 (I²C) + potentiometer (ADC) + button (GPIO) + LCD (I²C)».

| Компонент у Wokwi (пул методички) | Інтерфейс | Аналог у реальному житті (з методички) | Використано |
|---|---|---|---|
| LCD1602 (I²C) | I²C | Дисплей стану системи | ✅ `wokwi-lcd1602`, `pins: i2c`, 0x27 |
| Push button | GPIO input | Кнопка користувача B1 | ✅ `wokwi-pushbutton` на PC13 |
| UART Logger (built-in) | UART VCP | Заміна picocom | ✅ `$serialMonitor` на USART2 |
| MPU-6050 | I²C | Ідентичний до реального модуля | ✅ `wokwi-mpu6050`, 0x68 |
| Logic Analyzer | SPI/I²C/UART sniff | Заміна Saleae | ✅ `wokwi-logic-analyzer` |
| Potentiometer | ADC | Емулює будь-який аналоговий датчик | ✅ `wokwi-potentiometer` на PA0 |
| MQ-2 / MQ-135 Gas Sensor | ADC | Аналоговий газ-детектор | — не потрібен (ADC вже закрито потенціометром) |

Мінімум для здачі (3 компоненти з трьома різними інтерфейсами) виконано: **I²C** (MPU-6050, LCD1602) + **ADC** (потенціометр) + **GPIO** (кнопка). Компонентів поза пулом у схемі немає.

> Номери розділів/таблиць вказано за ревізіями документів у розділі 5.
> Перед здачею відкрийте кожен PDF і допишіть номер сторінки там, де стоїть «стор. —».

## 1. Мікроконтролер і плата

| Параметр | Значення | Джерело |
|---|---|---|
| Плата | NUCLEO-C031C6 (Nucleo-64, вбудований ST-Link V2-1 з VCP) | UM2953 |
| MCU | STM32C031C6T6: Arm Cortex-M0+, до 48 MHz, 32 KB Flash, 12 KB SRAM, без FPU | DS13867, розд. 1 «Introduction», табл. «Features and peripheral counts» |
| Живлення MCU | 2.0–3.6 V (на Nucleo — 3.3 V від LDO) | DS13867, розд. «General operating conditions» |
| Тактування | HSI48 /1 → SYSCLK 48 MHz, AHB /1, APB /1; 1 wait state flash | RM0490, розд. «RCC», розд. «Read access latency» |
| Alternate functions | див. таблицю пінів нижче | DS13867, табл. «Port A/B alternate function mapping» |

**Чому не NUCLEO-F411RE/F446RE.** Ці плати методичка дає для форми A. Для форми B вона вимагає «віртуальний Nucleo/STM32». Wokwi симулює лише три STM32: STM32C031 (Nucleo-C031C6), STM32L031 (Nucleo-L031K6) і STM32F103 (Blue Pill) ([docs.wokwi.com → Supported Hardware](https://docs.wokwi.com/getting-started/supported-hardware)). Єдина Nucleo-64 серед них — **NUCLEO-C031C6**. VCP на ній так само на USART2, LED на PA5, кнопка B1 на PC13.

## 2. Розводка пінів

| Сигнал | Пін MCU | Arduino-роз'єм | Периферія, AF | Компонент |
|---|---|---|---|---|
| VCP TX | PA2 | — (лише ST-Link VCP) | USART2_TX, AF1 | UART Logger |
| VCP RX | PA3 | — (лише ST-Link VCP) | USART2_RX, AF1 | UART Logger |
| I²C SCL | PB8 | D15 | I2C1_SCL, AF6, open-drain | MPU-6050, LCD1602 |
| I²C SDA | PB9 | D14 | I2C1_SDA, AF6, open-drain | MPU-6050, LCD1602 |
| Аналоговий вхід | PA0 | A0 | ADC1_IN0 | Potentiometer |
| Кнопка | PC13 | — | GPIO input, pull-up, active low | Push button (B1) |
| LED | PA5 | D13 | GPIO output | LD4 на платі |

Налаштування збігаються з кроком 2 методички: `USART2 → Asynchronous → 115200 8N1`, `I2C1 → Standard Mode 100 kHz`, `ADC1 IN0`.

Логічний аналізатор: D0 = SCL (PB8), D1 = SDA (PB9), D2 = UART_TX (PA2), D3 = кнопка (PC13).

## 3. Компоненти

### 3.1 MPU-6050 — IMU, I²C

| Параметр | Значення | Джерело |
|---|---|---|
| Інтерфейс | I²C, до 400 kHz (у нас 100 kHz Standard mode) | PS-MPU-6000A-00 rev 3.4, розд. «I²C Timing Characterization», стор. — |
| Адреса | 0x68 (AD0 = 0), 0x69 (AD0 = 1) | PS-MPU-6000A-00, розд. «I²C Interface», стор. — |
| Живлення | Чип: VDD 2.375–3.46 V. Модуль GY-521 має LDO, тому VCC = 3.3–5 V | PS-MPU-6000A-00, розд. «Electrical Specifications», стор. — |
| Струм | ~3.9 mA (гіро + акселерометр) | PS-MPU-6000A-00, розд. «Electrical Specifications», стор. — |
| Ідентифікація | `WHO_AM_I` (0x75) = 0x68 | RM-MPU-6000A-00 rev 4.2, Register 117, стор. 45 |
| Запуск вимірів | `PWR_MGMT_1` (0x6B) = 0x00 — вихід зі sleep | RM-MPU-6000A-00, Register 107, стор. 40 |
| Дані | `ACCEL_XOUT_H..ACCEL_ZOUT_L` (0x3B–0x40), ±2 g = 16384 LSB/g | RM-MPU-6000A-00, Registers 59–64, стор. 29 |
| Wokwi | `wokwi-mpu6050`, адреса 0x68; прискорення задаються повзунками | [docs.wokwi.com/parts/wokwi-mpu6050](https://docs.wokwi.com/parts/wokwi-mpu6050) |

### 3.2 LCD1602 з I²C-адаптером (PCF8574 + HD44780) — I²C

| Параметр | Значення | Джерело |
|---|---|---|
| Інтерфейс | I²C до 100 kHz (PCF8574) → 4-бітна шина HD44780 | PCF8574 datasheet (NXP / TI), розд. «Characteristics of the I²C-bus» |
| Адреса | 0x27 (A0..A2 = 1, PCF8574 без «A») | PCF8574 datasheet, розд. «Addressing»; Wokwi — атрибут `i2cAddress` |
| Розводка PCF8574 → HD44780 | P0 = RS, P1 = RW, P2 = E, P3 = підсвітка, P4..P7 = D4..D7 | [docs.wokwi.com/parts/wokwi-lcd1602](https://docs.wokwi.com/parts/wokwi-lcd1602) |
| Ініціалізація | > 40 мс після живлення, 3× `0x3`, `0x2` (4-bit), `0x28`, `0x08`, `0x01`, `0x06`, `0x0C` | HD44780U datasheet (Hitachi), Figure 24 «4-Bit Interface», стор. — |
| Затримки | Clear/Home ≤ 1.52 мс, інші команди 37 мкс | HD44780U datasheet, Table 6 «Instructions» |
| Живлення | 5 V (контраст LCD розрахований на 5 V) | HD44780U datasheet, «Electrical Characteristics» |
| Роль у ЛР1 | Дисплей стану: pitch/roll з MPU-6050, значення потенціометра, режим LIVE/HOLD | — |

### 3.3 Potentiometer — ADC

| Параметр | Значення | Джерело |
|---|---|---|
| Інтерфейс | ADC1_IN0, 12 bit, 0…VDDA (3.3 V) → 0…4095 | RM0490, розд. «Analog-to-digital converter (ADC)» |
| Налаштування | Тактування PCLK/2 = 24 MHz (≤ 35 MHz), sampling time 39.5 циклів | DS13867, розд. «ADC characteristics» |
| Живлення | 3V3 Nucleo (щоб вихід не перевищував VDDA) | — |
| Wokwi | `wokwi-potentiometer`, значення 0…1023 у повзунку → 0…VCC на SIG | [docs.wokwi.com/parts/wokwi-potentiometer](https://docs.wokwi.com/parts/wokwi-potentiometer) |

### 3.4 Push button і LED — GPIO

| Параметр | Значення | Джерело |
|---|---|---|
| Кнопка | PC13 (пін B1 USER на Nucleo), active low, внутрішній pull-up | UM2953, розд. «Push-buttons» |
| LED LD4 | PA5, active high | UM2953, розд. «LEDs» |
| Wokwi | `wokwi-pushbutton` між PC13 і GND, клавіша `h` | [docs.wokwi.com/parts/wokwi-pushbutton](https://docs.wokwi.com/parts/wokwi-pushbutton) |

### 3.5 UART Logger і Logic Analyzer

| Компонент | Налаштування |
|---|---|
| UART Logger | `$serialMonitor:TX → PA3`, `$serialMonitor:RX → PA2`, USART2 115200 8N1 |
| Logic Analyzer | 4 канали: SCL, SDA, UART_TX, BTN. Після зупинки симуляції зберігає `lr1-capture.vcd` (відкривається в PulseView) |

## 4. Перевірка інтерфейсів у прошивці

| Інтерфейс | Компонент | Перевірка |
|---|---|---|
| UART | UART Logger | банер, `[ OK ]` для кожної `MX_*_Init()`, `tick N` щосекунди |
| I²C | MPU-6050, LCD1602 | I²C-скан → `0x27 0x68`; `WHO_AM_I = 0x68`; текст на LCD |
| ADC | Potentiometer | `pot=0..4095` у лозі й на LCD |
| GPIO | Push button, LED | `[B1] HOLD/LIVE`, LED горить у HOLD |

## 5. Документи

1. STMicroelectronics, *DS13867 — STM32C031x4/x6 datasheet*. https://www.st.com/resource/en/datasheet/stm32c031c6.pdf
2. STMicroelectronics, *RM0490 — STM32C0x1 reference manual*. https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
3. STMicroelectronics, *UM2953 — STM32 Nucleo-64 board (MB1717)*. https://www.st.com/resource/en/user_manual/um2953-stm32-nucleo64-board-mb1717-stmicroelectronics.pdf
4. InvenSense, *MPU-6000/MPU-6050 Product Specification, PS-MPU-6000A-00 rev 3.4*. https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf
5. InvenSense, *MPU-6000/MPU-6050 Register Map and Descriptions, RM-MPU-6000A-00 rev 4.2*. https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf
6. NXP, *PCF8574; PCF8574A — Remote 8-bit I/O expander for I²C-bus*. https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf
7. Hitachi, *HD44780U (LCD-II) Dot Matrix Liquid Crystal Display Controller/Driver*. https://www.sparkfun.com/datasheets/LCD/HD44780.pdf
8. Wokwi Docs — STM32 Nucleo-64 C031C6. https://docs.wokwi.com/parts/board-st-nucleo-c031c6
9. Wokwi Docs — wokwi-lcd1602. https://docs.wokwi.com/parts/wokwi-lcd1602
