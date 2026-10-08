# Апаратні компоненти

Проєкт: **Motion-Mirroring Teleoperator** (тема 6 з Додатку A).
Форма виконання: **B — симулятор Wokwi**. Дані про реальні модулі наведені для перенесення на залізо в ЛР2–4.

> Номери розділів/таблиць вказано за ревізіями документів у колонці «Джерело».
> Перед здачею відкрийте кожен PDF і допишіть номер сторінки там, де він позначений як «стор. —».

## 1. Мікроконтролер і плата

| Параметр | Значення | Джерело |
|---|---|---|
| Плата | NUCLEO-C031C6 (Nucleo-64, вбудований ST-Link V2-1 з VCP) | UM2953 «STM32 Nucleo-64 board (MB1717)» |
| MCU | STM32C031C6T6: Arm Cortex-M0+, до 48 MHz, 32 KB Flash, 12 KB SRAM, без FPU | DS13867, розд. 1 «Introduction», табл. 2 «Features and peripheral counts» |
| Живлення MCU | 2.0–3.6 V (на Nucleo — 3.3 V від LDO) | DS13867, розд. 5.3.1 «General operating conditions» |
| Тактування | HSI48 /1 → SYSCLK 48 MHz, AHB /1, APB /1; 1 wait state flash | RM0490, розд. 5 «RCC», розд. 3.3.4 «Read access latency» |
| Alternate functions | див. таблицю пінів нижче | DS13867, табл. «Port A/B alternate function mapping» |

**Чому не NUCLEO-F411RE:** Wokwi симулює лише STM32C031, STM32L031 і STM32F103 ([docs.wokwi.com → Supported Hardware](https://docs.wokwi.com/getting-started/supported-hardware)). STM32F4 у симуляторі немає, тому для форми B обрано найближчий аналог — Nucleo-64 з тим самим розташуванням VCP (USART2, PA2/PA3), LED (PA5) та кнопки B1 (PC13). Прошивка на HAL переноситься на F411RE зміною `Drivers/` (STM32F4xx HAL), startup-файлу, лінкер-скрипта і `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`.

## 2. Розводка пінів

| Сигнал | Пін MCU | Arduino / Morpho | Периферія, AF | Компонент |
|---|---|---|---|---|
| VCP TX | PA2 | — (лише ST-Link VCP) | USART2_TX, AF1 | ST-Link VCP / Wokwi Serial Monitor |
| VCP RX | PA3 | — (лише ST-Link VCP) | USART2_RX, AF1 | ST-Link VCP / Wokwi Serial Monitor |
| I²C SCL | PB8 | D15 | I2C1_SCL, AF6, open-drain | MPU-6050 |
| I²C SDA | PB9 | D14 | I2C1_SDA, AF6, open-drain | MPU-6050 |
| Аналоговий вхід | PA0 | A0 | ADC1_IN0 | Потенціометр |
| PWM pan | PB4 | D5 | TIM3_CH1, AF1 | Серво pan |
| PWM tilt | PB5 | D6 | TIM3_CH2, AF1 | Серво tilt |
| SPI SCK | PB3 | D3 | SPI1_SCK, AF0 | W25Q64 (лише залізо) |
| SPI MISO | PA6 | D12 | SPI1_MISO, AF0 | W25Q64 (лише залізо) |
| SPI MOSI | PA7 | D11 | SPI1_MOSI, AF0 | W25Q64 (лише залізо) |
| SPI CS# | PB0 | D10 | GPIO output, idle high | W25Q64 (лише залізо) |
| LED | PA5 | D13 | GPIO output | LD4 на платі |
| Кнопка | PC13 | — | GPIO input, pull-up, active low | B1 USER |

PB4/PB5 обрано під серво, бо PA6/PA7 (типові TIM3_CH1/CH2) зайняті під SPI1 MISO/MOSI на Arduino-роз'ємі.

## 3. Компоненти

### 3.1 MPU-6050 (GY-521) — IMU, I²C

| Параметр | Значення | Джерело |
|---|---|---|
| Інтерфейс | I²C, до 400 kHz (у нас 100 kHz Standard mode) | PS-MPU-6000A-00 rev 3.4, розд. 6.5 «I²C Timing Characterization», стор. — |
| Адреса | 0x68 (AD0 = 0), 0x69 (AD0 = 1) | PS-MPU-6000A-00, розд. 9.2 «I²C Interface», стор. — |
| Живлення | Чип: VDD 2.375–3.46 V. Модуль GY-521 має LDO, тому VCC = 3.3–5 V | PS-MPU-6000A-00, розд. 6.3 «Electrical Specifications» |
| Струм | ~3.9 mA (гіро + акселерометр) | PS-MPU-6000A-00, розд. 6.3, стор. — |
| Ідентифікація | `WHO_AM_I` (0x75) = 0x68 | RM-MPU-6000A-00 rev 4.2, Register 117, стор. 45 |
| Запуск вимірів | `PWR_MGMT_1` (0x6B) = 0x00 — вихід зі sleep | RM-MPU-6000A-00, Register 107, стор. 40 |
| Дані | `ACCEL_XOUT_H..ACCEL_ZOUT_L` (0x3B–0x40), ±2 g = 16384 LSB/g | RM-MPU-6000A-00, Registers 59–64, стор. 29 |
| Wokwi | `wokwi-mpu6050`, адреса 0x68, кути задаються повзунками | [docs.wokwi.com/parts/wokwi-mpu6050](https://docs.wokwi.com/parts/wokwi-mpu6050) |

Живлення з 3V3 Nucleo — так, споживання кілька мА.

### 3.2 W25Q64JV — SPI NOR flash 64 Mbit (лише на залізі)

| Параметр | Значення | Джерело |
|---|---|---|
| Інтерфейс | SPI mode 0 або 3, Standard/Dual/Quad; до 133 MHz (у нас 3 MHz) | W25Q64JV datasheet, розд. 6.1 «Standard SPI Instructions» |
| Розводка | /CS, DO (MISO), /WP, GND, DI (MOSI), CLK, /HOLD (або /RESET), VCC | W25Q64JV, розд. 3 «Package Types and Pin Configurations» |
| Живлення | 2.7–3.6 V — **тільки 3.3 V**, 5 V пошкодить чип. /WP і /HOLD підтягнути до VCC | W25Q64JV, розд. 4 «Pin Descriptions», розд. 9 «Electrical Characteristics» |
| Ідентифікація | `Read JEDEC ID` (0x9F) → `EF 40 17` | W25Q64JV, розд. 8.2 «Read JEDEC ID (9Fh)» |
| Струм | до ~25 mA під час Program/Erase | W25Q64JV, розд. 9 «DC Electrical Characteristics», стор. — |
| Wokwi | **Відсутній.** Прошивка коректно повідомляє `JEDEC ID = FF FF FF (not connected)` | — |

### 3.3 SG92R — мікросерво, PWM

| Параметр | Значення | Джерело |
|---|---|---|
| Інтерфейс | PWM 50 Hz (період 20 мс), імпульс ≈ 0.5–2.4 мс ↔ 0–180° | Tower Pro SG92R product page |
| Живлення | 4.8–6 V. Від 5V Nucleo — лише для 1 серво без навантаження; 2 серво — окреме джерело 5 V зі спільною GND | Tower Pro SG92R |
| Налаштування у прошивці | TIM3: 48 MHz / 48 = 1 MHz, ARR = 19999, CCR = ширина імпульсу в мкс (544–2400) | RM0490, розд. «General-purpose timers (TIM3)» |
| Wokwi | `wokwi-servo` | [docs.wokwi.com/parts/wokwi-servo](https://docs.wokwi.com/parts/wokwi-servo) |

### 3.4 Потенціометр — ADC

| Параметр | Значення | Джерело |
|---|---|---|
| Інтерфейс | ADC1_IN0, 12 bit, 0…VDDA (3.3 V) → 0…4095 | RM0490, розд. «Analog-to-digital converter (ADC)» |
| Налаштування | Тактування PCLK/2 = 24 MHz (≤ 35 MHz), sampling time 39.5 циклів | DS13867, розд. 5.3 «ADC characteristics» |
| Роль у проєкті | ручне задання кута pan / калібрування нуля; на залізі заміна — GM5528 або друга вісь IMU | — |

### 3.5 Кнопка B1 і LED LD4 — GPIO

| Параметр | Значення | Джерело |
|---|---|---|
| B1 USER | PC13, active low (на Nucleo зовнішній pull-up + RC) | UM2953, розд. «Push-buttons» |
| LD4 | PA5, active high | UM2953, розд. «LEDs» |

## 4. Підсумок інтерфейсів

| Інтерфейс | Компонент у Wokwi | Перевірка в прошивці |
|---|---|---|
| UART | Serial Monitor | банер + `tick N` щосекунди |
| I²C | MPU-6050 | I²C scan → `0x68`, `WHO_AM_I = 0x68` |
| ADC | Потенціометр | `pot=0..4095` у лозі, серво pan повторює ручку |
| PWM (TIM) | 2× серво | серво рухаються; на логічному аналізаторі 50 Hz, 0.5–2.4 мс |
| GPIO | Кнопка B1, LED | `[B1] HOLD ON/OFF`, LED блимає / горить |
| SPI | — (на залізі W25Q64) | `JEDEC ID` |

## 5. Документи

1. STMicroelectronics, *DS13867 — STM32C031x4/x6 datasheet*. https://www.st.com/resource/en/datasheet/stm32c031c6.pdf
2. STMicroelectronics, *RM0490 — STM32C0x1 reference manual*. https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
3. STMicroelectronics, *UM2953 — STM32 Nucleo-64 board (MB1717)*. https://www.st.com/resource/en/user_manual/um2953-stm32-nucleo64-board-mb1717-stmicroelectronics.pdf
4. InvenSense, *MPU-6000/MPU-6050 Product Specification, PS-MPU-6000A-00 rev 3.4*. https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf
5. InvenSense, *MPU-6000/MPU-6050 Register Map and Descriptions, RM-MPU-6000A-00 rev 4.2*. https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf
6. Winbond, *W25Q64JV datasheet*. https://www.winbond.com/hq/product/code-storage-flash-memory/serial-nor-flash/
7. Wokwi Docs — STM32 Nucleo-64 C031C6. https://docs.wokwi.com/parts/board-st-nucleo-c031c6
