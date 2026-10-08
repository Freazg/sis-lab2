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

Компоненти — лише з пулу методички для форми B: MPU-6050, LCD1602 (I²C), Potentiometer, Push button, UART Logger, Logic Analyzer.

| Інтерфейс | Пін | Компонент | Що видно |
|---|---|---|---|
| USART2 115200 8N1 | PA2/PA3 | UART Logger | банер, `[ OK ]` для кожної `MX_*_Init()`, `tick N …` щосекунди |
| I2C1 100 kHz | PB8/PB9 | MPU-6050 (0x68), LCD1602 (0x27) | I²C-скан → `0x27 0x68`, `WHO_AM_I = 0x68`, pitch/roll і потенціометр на LCD |
| ADC1_IN0 | PA0 | Potentiometer | `pot=0..4095` |
| GPIO | PC13 / PA5 | Push button / LED LD4 | кнопка (або клавіша `h`) → LIVE/HOLD, LED горить у HOLD |

Логічний аналізатор пише SCL, SDA, UART_TX, BTN у `lr1-capture.vcd`. Файл відкривається в PulseView або GTKWave.

## Справжня плата (бонус)

Той самий ELF працює на фізичній NUCLEO-C031C6:

```bash
make flash-hw
```

```bash
make monitor
```

Чому C031C6, а не F411RE: Wokwi не симулює STM32F4. Деталі — у [docs/hardware-components.md](docs/hardware-components.md).
