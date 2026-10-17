# Motion-Mirroring Teleoperator — STM32 (NUCLEO-C031C6, Wokwi)

Курс «Системне програмування вбудованих систем», КПІ ім. Ігоря Сікорського, ІО-31, Сас Євгеній.

**ЛР2:** усі пристрої PRD в одній прошивці (MPU-6050, W25Q64, 2×SG92R, FT232RL, 2 LED, B1), самоперевірка,
відтворювані записи логічного аналізатора з декодуванням і аналізом, шарова архітектура, хостові тести, CI.

| Документ | Що всередині |
|---|---|
| [docs/lab2-report.md](docs/lab2-report.md) | звіт ЛР2 |
| [docs/pinout.md](docs/pinout.md) | піни, адреси, живлення, шини, схема, конфлікти, інструкція зі складання |
| [docs/architecture.md](docs/architecture.md) | шари, правила залежностей, component diagram |
| [docs/captures/analysis.md](docs/captures/analysis.md) | аналіз записів I²C/SPI/PWM/UART, аномалії A1–A6 |
| [docs/PRD.md](docs/PRD.md), [docs/hardware-components.md](docs/hardware-components.md) | вимоги і компоненти |

```
docker/            тулчейн-образ (arm-none-eabi-gcc, gcc, gcovr)
firmware/          Core/ Drivers/ Platform/ Devices/ App/ (docs/architecture.md)
chips/             моделі W25Q64JV і FT232RL для Wokwi (Chips API, за datasheet)
tests/             хостові тести Unity + test doubles + вектори з записів
tools/             capture.py, analyze_captures.py, make_test_vectors.py, check_deps.py, plot_waveforms.py
docs/captures/     *.vcd, сценарії, схеми щупів, декодування sigrok-cli, measurements.md
```

## Команди

```bash
make image        # один раз: Docker-образ
make build        # check-deps + прошивка (Wokwi)
make test         # хостові тести
make coverage     # тести + покриття (поріг 90 % рядків)
make sim          # Wokwi CLI: самоперевірка має дати ST,dev=SUMMARY,res=PASS
make captures     # перезаписати всі записи логічного аналізатора
make build-nucleo # для реальної плати; make flash-hw / make monitor
```

Для `make sim` / `make captures` потрібні [wokwi-cli](https://docs.wokwi.com/wokwi-ci/cli-installation), `sigrok-cli`
і токен `WOKWI_CLI_TOKEN` (або файл `~/.wokwi_token`).

## Справжня плата (бонус)

Для фізичної NUCLEO-C031C6 збірка `BOARD_TARGET=nucleo` (HAL з repeated START):

```bash
make flash-hw
```

```bash
make monitor
```

Чому C031C6, а не F411RE: Wokwi не симулює STM32F4. Деталі — у [docs/hardware-components.md](docs/hardware-components.md).
