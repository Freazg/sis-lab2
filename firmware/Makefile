##############################################################################
# firmware/Makefile — збірка прошивки NUCLEO-C031C6 (STM32C031C6, Cortex-M0+).
# Запускається всередині Docker-образу stm32-build (див. кореневий Makefile),
# але працює і з локальним arm-none-eabi-gcc: make -C firmware
##############################################################################

TARGET    = firmware
BUILD_DIR = build

DEBUG ?= 1
OPT   ?= -Os

# ---- Джерела ---------------------------------------------------------------
HAL_DIR = Drivers/STM32C0xx_HAL_Driver/Src

C_SOURCES = \
Core/Src/main.c \
Core/Src/stm32c0xx_it.c \
Core/Src/stm32c0xx_hal_msp.c \
Core/Src/system_stm32c0xx.c \
Core/Src/syscalls.c \
$(HAL_DIR)/stm32c0xx_hal.c \
$(HAL_DIR)/stm32c0xx_hal_cortex.c \
$(HAL_DIR)/stm32c0xx_hal_rcc.c \
$(HAL_DIR)/stm32c0xx_hal_rcc_ex.c \
$(HAL_DIR)/stm32c0xx_hal_flash.c \
$(HAL_DIR)/stm32c0xx_hal_flash_ex.c \
$(HAL_DIR)/stm32c0xx_hal_gpio.c \
$(HAL_DIR)/stm32c0xx_hal_exti.c \
$(HAL_DIR)/stm32c0xx_hal_dma.c \
$(HAL_DIR)/stm32c0xx_hal_dma_ex.c \
$(HAL_DIR)/stm32c0xx_hal_pwr.c \
$(HAL_DIR)/stm32c0xx_hal_pwr_ex.c \
$(HAL_DIR)/stm32c0xx_hal_uart.c \
$(HAL_DIR)/stm32c0xx_hal_uart_ex.c \
$(HAL_DIR)/stm32c0xx_hal_i2c.c \
$(HAL_DIR)/stm32c0xx_hal_i2c_ex.c \
$(HAL_DIR)/stm32c0xx_hal_adc.c \
$(HAL_DIR)/stm32c0xx_hal_adc_ex.c \
$(HAL_DIR)/stm32c0xx_ll_adc.c \
$(HAL_DIR)/stm32c0xx_hal_tim.c \
$(HAL_DIR)/stm32c0xx_hal_tim_ex.c \
$(HAL_DIR)/stm32c0xx_hal_spi.c \
$(HAL_DIR)/stm32c0xx_hal_spi_ex.c

ASM_SOURCES = startup_stm32c031xx.s

LDSCRIPT = STM32C031C6Tx_FLASH.ld

# ---- Тулчейн ---------------------------------------------------------------
PREFIX = arm-none-eabi-
CC   = $(PREFIX)gcc
AS   = $(PREFIX)gcc -x assembler-with-cpp
CP   = $(PREFIX)objcopy
SZ   = $(PREFIX)size
HEX  = $(CP) -O ihex
BIN  = $(CP) -O binary -S

# ---- Прапорці --------------------------------------------------------------
# Cortex-M0+ не має FPU — float рахується програмно (soft-float).
MCU = -mcpu=cortex-m0plus -mthumb

C_DEFS = \
-DUSE_HAL_DRIVER \
-DSTM32C031xx

C_INCLUDES = \
-ICore/Inc \
-IDrivers/STM32C0xx_HAL_Driver/Inc \
-IDrivers/STM32C0xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32C0xx/Include \
-IDrivers/CMSIS/Include

ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -std=gnu11 -Wall -fdata-sections -ffunction-sections
# Відтворювані збірки: шляхи до джерел не потрапляють в ELF у вигляді абсолютних.
CFLAGS += -ffile-prefix-map=$(CURDIR)=.

ifeq ($(DEBUG), 1)
CFLAGS += -g3 -gdwarf-4
endif

# Генерувати файли залежностей (.d), щоб зміна .h перезбирала потрібні .c
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LIBS    = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) $(LIBS) \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections \
          -Wl,--print-memory-usage

# ---- Правила ---------------------------------------------------------------
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin
	@cd $(BUILD_DIR) && sha256sum $(TARGET).bin $(TARGET).hex

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile $(LDSCRIPT)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	-rm -rf $(BUILD_DIR)

.PHONY: all clean

-include $(wildcard $(BUILD_DIR)/*.d)
