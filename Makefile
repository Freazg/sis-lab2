##############################################################################
# Кореневий Makefile — обгортка над Docker-тулчейном.
#
#   make image    — один раз зібрати образ stm32-build (docker/Dockerfile)
#   make build    — зібрати прошивку в контейнері -> firmware/build/firmware.{elf,hex,bin}
#   make sim      — headless-прогін у Wokwi CLI (потрібен WOKWI_CLI_TOKEN), вивід UART у консоль
#   make flash    — Форма B: «прошивка» = перезапуск симулятора з новим ELF (див. нижче)
#   make flash-hw — прошити справжню NUCLEO-C031C6 через ST-Link (st-flash)
#   make monitor  — UART-термінал до справжньої плати (picocom, 115200 8N1)
#   make clean    — прибрати артефакти збірки
##############################################################################

IMAGE      ?= stm32-build
FW_DIR     := firmware
BUILD_DIR  := $(FW_DIR)/build
ELF        := $(BUILD_DIR)/firmware.elf
BIN        := $(BUILD_DIR)/firmware.bin

# Для make monitor: make monitor PORT=/dev/tty.usbmodem1103
PORT       ?= $(firstword $(wildcard /dev/tty.usbmodem* /dev/ttyACM*))
SIM_TIMEOUT ?= 10000

# --user: артефакти належать поточному користувачу, а не root.
DOCKER_RUN = docker run --rm -v "$(CURDIR)":/workspace -w /workspace \
             --user $(shell id -u):$(shell id -g) $(IMAGE)

.PHONY: all image build clean sim flash flash-hw monitor

all: build

image:
	docker build -t $(IMAGE) docker/

build:
	$(DOCKER_RUN) make -C $(FW_DIR) all

clean:
	$(DOCKER_RUN) make -C $(FW_DIR) clean

# Wokwi CLI: https://docs.wokwi.com/wokwi-ci/cli-installation
# Токен: https://wokwi.com/dashboard/ci  ->  export WOKWI_CLI_TOKEN=...
sim: $(ELF)
	wokwi-cli --timeout $(SIM_TIMEOUT) --expect-text "tick 3" .

flash: $(ELF)
	@echo "Форма B (Wokwi): ELF готовий -> $(ELF)"
	@echo "У VS Code: F1 -> 'Wokwi: Start Simulator' (або 'Restart'), щоб запустити нову прошивку."

flash-hw: $(BIN)
	st-flash --reset write $(BIN) 0x08000000

monitor:
	picocom -b 115200 --imap lfcrlf $(PORT)

$(ELF) $(BIN):
	$(MAKE) build
