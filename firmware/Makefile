##############################################################################
# firmware/Makefile — збірка прошивки NUCLEO-C031C6 (STM32C031C6, Cortex-M0+).
# Запускається в Docker-образі stm32-build (кореневий Makefile), але працює
# і з локальним arm-none-eabi-gcc:  make -C firmware
#
# Шари (docs/architecture.md):
#   Core/      — код у структурі STM32CubeMX (main, MSP, IRQ)
#   Drivers/   — CMSIS + HAL від ST (вендорський, не редагується)
#   Platform/  — адаптація до плати: піни, обгортки шин над HAL
#   Devices/   — драйвери пристроїв, залежать лише від Devices/bus.h
#   App/       — прикладна логіка, не знає про HAL
#
# BOARD_TARGET=wokwi  (за замовчуванням) — I2C без repeated START (обмеження моделі Wokwi)
# BOARD_TARGET=nucleo — реальна плата, HAL_I2C_Mem_Read з repeated START
##############################################################################

TARGET       = firmware
BUILD_DIR    = build
BOARD_TARGET ?= wokwi

DEBUG ?= 1
OPT   ?= -Os

# ---- Джерела -----------------------------------------------------------------
HAL_DIR = Drivers/STM32C0xx_HAL_Driver/Src

OWN_SOURCES = \
Core/Src/main.c \
Core/Src/stm32c0xx_it.c \
Core/Src/stm32c0xx_hal_msp.c \
$(wildcard Platform/*.c) \
$(wildcard Devices/*.c) \
$(wildcard App/*.c)

VENDOR_SOURCES = \
Core/Src/system_stm32c0xx.c \
$(HAL_DIR)/stm32c0xx_hal.c \
$(HAL_DIR)/stm32c0xx_hal_cortex.c \
$(HAL_DIR)/stm32c0xx_hal_rcc.c \
$(HAL_DIR)/stm32c0xx_hal_rcc_ex.c \
$(HAL_DIR)/stm32c0xx_hal_flash.c \
$(HAL_DIR)/stm32c0xx_hal_flash_ex.c \
$(HAL_DIR)/stm32c0xx_hal_gpio.c \
$(HAL_DIR)/stm32c0xx_hal_dma.c \
$(HAL_DIR)/stm32c0xx_hal_dma_ex.c \
$(HAL_DIR)/stm32c0xx_hal_pwr.c \
$(HAL_DIR)/stm32c0xx_hal_pwr_ex.c \
$(HAL_DIR)/stm32c0xx_hal_uart.c \
$(HAL_DIR)/stm32c0xx_hal_uart_ex.c \
$(HAL_DIR)/stm32c0xx_hal_i2c.c \
$(HAL_DIR)/stm32c0xx_hal_i2c_ex.c \
$(HAL_DIR)/stm32c0xx_hal_spi.c \
$(HAL_DIR)/stm32c0xx_hal_spi_ex.c \
$(HAL_DIR)/stm32c0xx_hal_tim.c \
$(HAL_DIR)/stm32c0xx_hal_tim_ex.c

ASM_SOURCES = startup_stm32c031xx.s
LDSCRIPT    = STM32C031C6Tx_FLASH.ld

# ---- Тулчейн -----------------------------------------------------------------
PREFIX = arm-none-eabi-
CC   = $(PREFIX)gcc
AS   = $(PREFIX)gcc -x assembler-with-cpp
CP   = $(PREFIX)objcopy
SZ   = $(PREFIX)size
HEX  = $(CP) -O ihex
BIN  = $(CP) -O binary -S

# ---- Прапорці ----------------------------------------------------------------
MCU = -mcpu=cortex-m0plus -mthumb

C_DEFS = -DUSE_HAL_DRIVER -DSTM32C031xx
ifeq ($(BOARD_TARGET),nucleo)
C_DEFS += -DBOARD_I2C_REPEATED_START=1 -DBOARD_I2C_HAL_TRANSMIT=1
else ifeq ($(BOARD_TARGET),wokwi)
C_DEFS += -DBOARD_I2C_REPEATED_START=0 -DBOARD_I2C_HAL_TRANSMIT=0
else
$(error BOARD_TARGET має бути wokwi або nucleo)
endif

C_INCLUDES = \
-ICore/Inc \
-IPlatform \
-IDevices \
-IApp \
-IDrivers/STM32C0xx_HAL_Driver/Inc \
-IDrivers/STM32C0xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32C0xx/Include \
-IDrivers/CMSIS/Include

ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -std=gnu11 -fdata-sections -ffunction-sections
CFLAGS += -ffile-prefix-map=$(CURDIR)=.
ifeq ($(DEBUG), 1)
CFLAGS += -g3 -gdwarf-4
endif
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

# Власний код: усі попередження — помилки.
OWN_WARN    = -Wall -Wextra -Werror -Wshadow -Wformat=2
# Вендорський код ST: -Wall -Wextra лишаються, придушено лише те, що ST
# свідомо допускає (невикористані параметри у weak-колбеках HAL).
VENDOR_WARN = -Wall -Wextra -Wno-unused-parameter

LIBS    = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) $(LIBS) \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -Wl,--print-memory-usage

# ---- Правила -----------------------------------------------------------------
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin
	@cd $(BUILD_DIR) && sha256sum $(TARGET).bin $(TARGET).hex

OWN_OBJECTS    = $(addprefix $(BUILD_DIR)/own/,$(notdir $(OWN_SOURCES:.c=.o)))
VENDOR_OBJECTS = $(addprefix $(BUILD_DIR)/vendor/,$(notdir $(VENDOR_SOURCES:.c=.o)))
ASM_OBJECTS    = $(addprefix $(BUILD_DIR)/vendor/,$(notdir $(ASM_SOURCES:.s=.o)))
OBJECTS        = $(OWN_OBJECTS) $(VENDOR_OBJECTS) $(ASM_OBJECTS)

vpath %.c $(sort $(dir $(OWN_SOURCES) $(VENDOR_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/own/%.o: %.c Makefile | $(BUILD_DIR)/own
	$(CC) -c $(CFLAGS) $(OWN_WARN) $< -o $@

$(BUILD_DIR)/vendor/%.o: %.c Makefile | $(BUILD_DIR)/vendor
	$(CC) -c $(CFLAGS) $(VENDOR_WARN) $< -o $@

$(BUILD_DIR)/vendor/%.o: %.s Makefile | $(BUILD_DIR)/vendor
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile $(LDSCRIPT)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(BIN) $< $@

$(BUILD_DIR)/own $(BUILD_DIR)/vendor:
	mkdir -p $@

clean:
	-rm -rf $(BUILD_DIR)

.PHONY: all clean

-include $(wildcard $(BUILD_DIR)/*/*.d)
