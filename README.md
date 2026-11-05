# Motion-Mirroring Teleoperator — STM32 (NUCLEO-C031C6, Wokwi)

Курс «Системне програмування вбудованих систем», КПІ ім. Ігоря Сікорського, ІО-31, Сас Євгеній.

**ЛР4:** той самий продукт на FreeRTOS V11.3.1 (порт ARM_CM0, heap_4): сім задач із пріоритетами, черги, м'ютекси, сповіщення з ISR; обидві прошивки виміряно тими самими сценаріями в один день і порівняно. Звіт — [docs/lab4-report.md](docs/lab4-report.md). Прошивка ЛР3 — тег `lab3`.

**ЛР3:** продукт працює як ціле на super loop: режими LIVE/HOLD/REC/PLAY, комплементарний фільтр, запис траєкторії у W25Q64, CLI, виміри часу, пам'яті й меж підходу. Звіт — [docs/lab3-report.md](docs/lab3-report.md).

**ЛР2:** усі пристрої PRD в одній прошивці (MPU-6050, W25Q64, 2×SG92R, FT232RL, 2 LED, B1), самоперевірка,
відтворювані записи логічного аналізатора з декодуванням і аналізом, шарова архітектура, хостові тести, CI.

| Документ | Що всередині |
|---|---|
| [docs/lab4-report.md](docs/lab4-report.md) | звіт ЛР4 ([PDF](docs/lab4-report.pdf), [відеозвіт](https://drive.google.com/file/d/1MQMoJRLcpDYDiYF__TS3ai4pAc1USsIo/view?usp=sharing)) |
| [docs/rtos-design.md](docs/rtos-design.md) | задачі, пріоритети, механізми взаємодії, конфігурація ядра |
| [docs/comparison.md](docs/comparison.md) | порівняльна таблиця super loop і FreeRTOS, метрики ОС, сценарії §7.2 |
| [docs/metrics/memory-lr4.md](docs/metrics/memory-lr4.md) | пам'ять ядра, стеків і heap ОС |
| [docs/lab3-report.md](docs/lab3-report.md) | звіт ЛР3 ([PDF](docs/lab3-report.pdf), [відеозвіт](https://drive.google.com/file/d/1JDDGTTT_tXXBkp9Qv6n2L_oGPKjgLpMF/view?usp=sharing)) |
| [docs/tasks.md](docs/tasks.md) | задачі, періоди, дедлайни, переривання |
| [docs/metrics/results.md](docs/metrics/results.md) | результати S1–S3 і сценаріїв 6.2 |
| [docs/limitations.md](docs/limitations.md), [docs/baseline-lab4.md](docs/baseline-lab4.md) | реєстр обмежень, базова лінія для ЛР4 |
| [docs/lab2-report.md](docs/lab2-report.md) | звіт ЛР2 ([PDF](docs/lab2-report.pdf), [відеозвіт](https://drive.google.com/file/d/1oGNvfJEP8LUogLoFS4Q4c2V6_uePuqIl/view?usp=sharing)) |
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
git submodule update --init   # ядро FreeRTOS (firmware/Middlewares/FreeRTOS)
make image        # один раз: Docker-образ
make build        # check-deps + прошивка (Wokwi)
make test         # хостові тести
make coverage     # тести + покриття (поріг 90 % рядків)
make sim          # Wokwi CLI: самоперевірка має дати ST,dev=SUMMARY,res=PASS
make captures     # перезаписати записи логічного аналізатора ЛР2
make scenarios    # сценарії ЛР3 у Wokwi: docs/metrics/runs, docs/captures/lr3-*
make metrics      # зведення: docs/metrics/results.md
make mem          # пам'ять: docs/metrics/memory.md
make build-nucleo # для реальної плати; make flash-hw / make monitor
python3 tools/run_scenarios.py --fw lr3|lr4 [ім'я ...]   # ЛР4: сценарії обох прошивок, docs/metrics/runs-<fw>/
python3 tools/analyze_metrics.py --fw lr3|lr4 && python3 tools/compare.py   # docs/comparison.md
```

Для `make sim` / `make scenarios` потрібні [wokwi-cli](https://docs.wokwi.com/wokwi-ci/cli-installation), `sigrok-cli`
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
