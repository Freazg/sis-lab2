##############################################################################
# Кореневий Makefile — обгортка над Docker-тулчейном.
#
#   make image        — один раз зібрати образ stm32-build (docker/Dockerfile)
#   make build        — перевірка залежностей шарів + прошивка (Wokwi) у контейнері
#   make build-nucleo — прошивка для реальної NUCLEO-C031C6 (HAL з repeated START)
#   make test         — хостові тести (Unity) у контейнері
#   make coverage     — тести + звіт покриття gcovr (поріг у tests/Makefile)
#   make check-deps   — лише перевірка правил залежностей між шарами
#   make chips        — зібрати custom chips Wokwi (W25Q64JV, FT232RL) у WASM
#   make sim          — headless-прогін у Wokwi CLI: чекає ST,dev=SUMMARY,res=PASS
#   make captures     — перезаписати всі docs/captures/*.vcd і декодувати sigrok-cli
#   make vectors      — оновити tests/vectors/ з декодованих записів
#   make flash        — Форма B: «прошивка» = перезапуск симулятора з новим ELF
#   make flash-hw     — прошити справжню плату через ST-LINK (st-flash)
#   make monitor      — UART-термінал до справжньої плати (picocom, 115200 8N1)
##############################################################################

IMAGE      ?= stm32-build
FW_DIR     := firmware
BUILD_DIR  := $(FW_DIR)/build
ELF        := $(BUILD_DIR)/firmware.elf
BIN        := $(FW_DIR)/build-nucleo/firmware.bin

PORT       ?= $(firstword $(wildcard /dev/tty.usbmodem* /dev/ttyACM*))
SIM_TIMEOUT ?= 5000
WOKWI_CLI  ?= $(shell command -v wokwi-cli 2>/dev/null || echo $(HOME)/.wokwi/bin/wokwi-cli)
WOKWI_TOKEN = $(or $(WOKWI_CLI_TOKEN),$(shell cat $(HOME)/.wokwi_token 2>/dev/null))

# --user: артефакти належать поточному користувачу, а не root.
DOCKER_RUN = docker run --rm -v "$(CURDIR)":/workspace -w /workspace \
             --user $(shell id -u):$(shell id -g) $(IMAGE)

.PHONY: all image build build-nucleo test coverage check-deps chips sim captures vectors \
        flash flash-hw monitor clean

all: build

image:
	docker build -t $(IMAGE) docker/

check-deps:
	$(DOCKER_RUN) python3 tools/check_deps.py

build: check-deps
	$(DOCKER_RUN) make -C $(FW_DIR) all BOARD_TARGET=wokwi

build-nucleo: check-deps
	$(DOCKER_RUN) make -C $(FW_DIR) all BOARD_TARGET=nucleo BUILD_DIR=build-nucleo

test:
	$(DOCKER_RUN) make -C tests run

coverage:
	$(DOCKER_RUN) make -C tests coverage

chips:
	cd chips/w25q64 && $(WOKWI_CLI) chip compile w25q64.chip.c -o ../dist/w25q64.chip.wasm
	cd chips/ft232rl && $(WOKWI_CLI) chip compile ft232rl.chip.c -o ../dist/ft232rl.chip.wasm

sim: $(ELF) chips/dist/w25q64.chip.wasm
	WOKWI_CLI_TOKEN=$(WOKWI_TOKEN) $(WOKWI_CLI) --timeout $(SIM_TIMEOUT) \
	  --expect-text "ST,dev=SUMMARY,res=PASS" --fail-text "res=FAIL" .

captures: build build-nucleo chips/dist/w25q64.chip.wasm
	python3 tools/capture.py
	python3 tools/make_test_vectors.py

vectors:
	python3 tools/make_test_vectors.py

flash: $(ELF)
	@echo "Форма B (Wokwi): ELF готовий -> $(ELF)"
	@echo "VS Code: F1 -> 'Wokwi: Start Simulator', або headless: make sim"

flash-hw: build-nucleo
	st-flash --reset write $(BIN) 0x08000000

monitor:
	picocom -b 115200 --imap lfcrlf $(PORT)

chips/dist/w25q64.chip.wasm:
	$(MAKE) chips

$(ELF):
	$(MAKE) build

clean:
	$(DOCKER_RUN) sh -c "make -C $(FW_DIR) clean && make -C $(FW_DIR) clean BUILD_DIR=build-nucleo && make -C tests clean"
