# ЛР1 — Bring-up STM32 (Wokwi) · Motion-Mirroring Teleoperator

Прошивка на **STM32 HAL (C)** для **NUCLEO-C031C6** у симуляторі **Wokwi** (форма B).
Збирається в Docker. На хості не треба нічого ставити, крім Docker і VS Code.

```
/docker/Dockerfile              тулчейн-образ (ubuntu:22.04 + gcc-arm-none-eabi + newlib + srecord)
/firmware/                      HAL-проєкт у структурі CubeMX (Core/, Drivers/, startup, .ld, Makefile)
/docs/PRD.md                    вимоги до продукту
/docs/hardware-components.md    компоненти, піни, посилання на datasheet
/docs/report.md                 звіт ЛР1
/docs/screenshots/              скріншоти CubeMX / Wokwi / логічного аналізатора
/diagram.json, /wokwi.toml      схема і конфіг симулятора
/Makefile                       обгортка над Docker
```

## Швидкий старт

Перевірити, що Docker daemon запущений (на macOS — відкрити Docker Desktop):

```bash
docker info
```

Один раз зібрати образ тулчейну:

```bash
make image
```

Зібрати прошивку (`firmware/build/firmware.{elf,hex,bin}` + SHA-256):

```bash
make build
```

Запустити в симуляторі:
1. Встановити розширення **Wokwi for VS Code** і активувати ліцензію (`F1 → Wokwi: Request a new License`).
2. Відкрити теку проєкту у VS Code → `F1 → Wokwi: Start Simulator`.
3. У Serial Monitor з'явиться звіт bring-up і рядки `tick N` щосекунди.

Без GUI, через [Wokwi CLI](https://docs.wokwi.com/wokwi-ci/cli-installation) (потрібен `WOKWI_CLI_TOKEN`):

```bash
make sim
```

## Що робить прошивка

| Інтерфейс | Пін | Що видно |
|---|---|---|
| USART2 115200 8N1 | PA2/PA3 | банер, таблиця `[ OK ]` для кожного `MX_*_Init()`, `tick N …` |
| I2C1 100 kHz | PB8/PB9 | I²C-скан → `0x68`, `MPU-6050: WHO_AM_I = 0x68 (match)` |
| ADC1_IN0 | PA0 | `pot=0..4095`; серво pan повторює потенціометр |
| TIM3 PWM 50 Hz | PB4/PB5 | 2 серво; tilt повторює нахил MPU-6050 (pitch) |
| GPIO | PC13 / PA5 | B1 (або клавіша `h`) → HOLD; LED блимає в LIVE, горить у HOLD |
| SPI1 mode 0 | PB3/PA6/PA7, CS PB0 | `W25Q64: JEDEC ID …` (у Wokwi чипа немає → `not connected`) |

Логічний аналізатор у схемі пише SCL, SDA, PWM_PAN, UART_TX у `lr1-capture.vcd`. Файл відкривається в PulseView або GTKWave.

## Справжня плата (бонус)

Той самий ELF працює на фізичній NUCLEO-C031C6:

```bash
make flash-hw
```

```bash
make monitor
```

Чому C031C6, а не F411RE: Wokwi не симулює STM32F4. Деталі — у [docs/hardware-components.md](docs/hardware-components.md).
