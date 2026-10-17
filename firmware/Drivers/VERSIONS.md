# Версії вендорського коду (vendoring із записом версії)

| Компонент | Джерело | Коміт / тег | Шлях у репозиторії |
|---|---|---|---|
| STM32C0 HAL | https://github.com/STMicroelectronics/stm32c0xx-hal-driver | `a6c02b4` (HAL v1.4.1, `HAL_GetHalVersion()` = 1.4.1) | `firmware/Drivers/STM32C0xx_HAL_Driver/` |
| CMSIS Device C0 | https://github.com/STMicroelectronics/cmsis-device-c0 | `bcc8d94` | `firmware/Drivers/CMSIS/Device/ST/STM32C0xx/`, `startup_stm32c031xx.s`, `Core/Src/system_stm32c0xx.c` |
| CMSIS-Core (CMSIS 6) | https://github.com/STMicroelectronics/cmsis-core | `01d1814` | `firmware/Drivers/CMSIS/Include/` |
| Unity (тести) | https://github.com/ThrowTheSwitch/Unity | `v2.6.1` = `cbcd08f` | `tests/vendor/unity/` |
| Тулчейн | Docker `ubuntu:22.04`, `gcc-arm-none-eabi` 10.3-2021.07-4, gcc 11, gcovr 5.0 | `docker/Dockerfile` | — |
| Wokwi CLI | https://github.com/wokwi/wokwi-cli | v0.28.1 | лише на хості (`make sim`, `make captures`) |
| sigrok-cli / PulseView | Homebrew `sigrok-cli` 0.7.2; PulseView 0.4.2 (sigrok.org) | — | лише на хості (декодування) |

Вендорські файли не редагуються; конфігурація HAL — лише `Core/Inc/stm32c0xx_hal_conf.h`.
