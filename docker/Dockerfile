# Тулчейн-образ для збірки прошивки STM32 і хостових тестів.
# Збирається один раз: make image  (або docker build -t stm32-build docker/)
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Базовий набір — з методички ЛР1 (крос-компілятор, newlib, srecord).
RUN apt-get update && apt-get install -y --no-install-recommends \
      make gcc-arm-none-eabi binutils-arm-none-eabi \
      libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
      python3 git ca-certificates srecord \
    && rm -rf /var/lib/apt/lists/*

# Доповнення ЛР2: хостовий gcc для тестів (make test) і gcovr для звіту покриття.
RUN apt-get update && apt-get install -y --no-install-recommends \
      gcc libc6-dev gcovr \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
