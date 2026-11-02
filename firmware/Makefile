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
#   App/       — прикладна логіка, не знає про HAL і ОС
#   Rtos/      — задачі FreeRTOS, черги, м'ютекси, хуки (ЛР4)
#   Middlewares/FreeRTOS — ядро FreeRTOS-Kernel V11.3.1 (git submodule), порт GCC/ARM_CM0
#
# BOARD_TARGET=wokwi  (за замовчуванням) — I2C без repeated START (обмеження моделі Wokwi)
# BOARD_TARGET=nucleo — реальна плата, HAL_I2C_Mem_Read з repeated START
##############################################################################

TARGET       = firmware
BUILD_DIR    = build
BOARD_TARGET ?= wokwi

DEBUG ?= 1
OPT   ?= -Os
# LTO для власного коду і HAL: без нього прошивка з ядром ОС не вміщається у 32 КБ флеш.
# Ядро FreeRTOS збирається без LTO, щоб map-файл показував внесок tasks.o, queue.o, port.o (§6.5).
LTO   ?= 1
ifeq ($(LTO),1)
LTO_FLAGS = -flto
endif
# Варіанти для сценаріїв ОС (методичка ЛР4 §7.2): make RTOS_DEFS="-DRTOS_TICK_HZ=100"
# Сценарії інверсії, голодування і черги: RTOS_DEFS="-DRTOS_FAULTS=1 -DAPP_CAP=0"
RTOS_DEFS ?=
GIT_HASH ?= unknown # git-хеш для банера і логів (методичка ЛР3 §5.2), задає кореневий Makefile

# ---- Джерела -----------------------------------------------------------------
HAL_DIR = Drivers/STM32C0xx_HAL_Driver/Src

OWN_SOURCES = \
Core/Src/main.c \
Core/Src/stm32c0xx_it.c \
Core/Src/stm32c0xx_hal_msp.c \
$(wildcard Platform/*.c) \
$(wildcard Devices/*.c) \
$(wildcard App/*.c) \
$(wildcard Rtos/*.c)

RTOS_DIR = Middlewares/FreeRTOS
KERNEL_SOURCES = \
$(RTOS_DIR)/tasks.c \
$(RTOS_DIR)/queue.c \
$(RTOS_DIR)/list.c \
$(RTOS_DIR)/portable/MemMang/heap_4.c \
$(RTOS_DIR)/portable/GCC/ARM_CM0/port.c \
$(RTOS_DIR)/portable/GCC/ARM_CM0/portasm.c

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

C_DEFS = -DUSE_HAL_DRIVER -DSTM32C031xx -DFW_GIT_HASH=\"$(strip $(GIT_HASH))\" $(RTOS_DEFS)
ifeq ($(BOARD_TARGET),nucleo)
C_DEFS += -DBOARD_I2C_REPEATED_START=1 -DBOARD_WOKWI=0
else ifeq ($(BOARD_TARGET),wokwi)
C_DEFS += -DBOARD_I2C_REPEATED_START=0 -DBOARD_WOKWI=1
# Модель Cortex-M0+ у Wokwi ігнорує PRIMASK: макроси маскування порту замінено (Rtos/wokwi/portmacro.h)
WOKWI_INC = -IRtos/wokwi
else
$(error BOARD_TARGET має бути wokwi або nucleo)
endif

C_INCLUDES = $(WOKWI_INC) \
-ICore/Inc \
-IPlatform \
-IDevices \
-IApp \
-IRtos \
-I$(RTOS_DIR)/include \
-I$(RTOS_DIR)/portable/GCC/ARM_CM0 \
-IDrivers/STM32C0xx_HAL_Driver/Inc \
-IDrivers/STM32C0xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32C0xx/Include \
-IDrivers/CMSIS/Include

ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -std=gnu11 -fdata-sections -ffunction-sections
CFLAGS += -ffile-prefix-map=$(CURDIR)=.
# Статична оцінка стека (методичка ЛР3 §5.4): .su — кадр кожної функції, .ci — граф викликів.
# З LTO=1 .su створюються лише для ядра; стек задач ЛР4 міряє uxTaskGetStackHighWaterMark (§6.3).
CFLAGS += -fstack-usage -fcallgraph-info=su
ifeq ($(DEBUG), 1)
CFLAGS += -g3 -gdwarf-4
endif
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

# Власний код: усі попередження — помилки.
OWN_WARN    = -Wall -Wextra -Werror -Wshadow -Wformat=2 -Wstack-usage=512
# Вендорський код ST: -Wall -Wextra лишаються, придушено лише те, що ST
# свідомо допускає (невикористані параметри у weak-колбеках HAL).
VENDOR_WARN = -Wall -Wextra -Wno-unused-parameter

LIBS    = -lc -lm -lnosys
LDFLAGS = $(MCU) $(OPT) $(LTO_FLAGS) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) $(LIBS) \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -Wl,--print-memory-usage

# ---- Правила -----------------------------------------------------------------
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin
	@cd $(BUILD_DIR) && sha256sum $(TARGET).bin $(TARGET).hex

OWN_OBJECTS    = $(addprefix $(BUILD_DIR)/own/,$(notdir $(OWN_SOURCES:.c=.o)))
VENDOR_OBJECTS = $(addprefix $(BUILD_DIR)/vendor/,$(notdir $(VENDOR_SOURCES:.c=.o)))
KERNEL_OBJECTS = $(addprefix $(BUILD_DIR)/kernel/,$(notdir $(KERNEL_SOURCES:.c=.o)))
ASM_OBJECTS    = $(addprefix $(BUILD_DIR)/vendor/,$(notdir $(ASM_SOURCES:.s=.o)))
OBJECTS        = $(OWN_OBJECTS) $(VENDOR_OBJECTS) $(KERNEL_OBJECTS) $(ASM_OBJECTS)

vpath %.c $(sort $(dir $(OWN_SOURCES) $(VENDOR_SOURCES) $(KERNEL_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/own/%.o: %.c Makefile | $(BUILD_DIR)/own
	$(CC) -c $(CFLAGS) $(LTO_FLAGS) $(OWN_WARN) $< -o $@

$(BUILD_DIR)/vendor/%.o: %.c Makefile | $(BUILD_DIR)/vendor
	$(CC) -c $(CFLAGS) $(LTO_FLAGS) $(VENDOR_WARN) $< -o $@

# Ядро: без LTO; SysTick_Handler порту перейменовано, щоб Core/Src/stm32c0xx_it.c міряв тривалість тіку
$(BUILD_DIR)/kernel/port.o: KERNEL_DEFS = -DSysTick_Handler=xPortSysTickHandler
ifeq ($(BOARD_TARGET),wokwi)
$(BUILD_DIR)/kernel/portasm.o: KERNEL_DEFS = -DPendSV_Handler=xPortPendSVHandler
endif
$(BUILD_DIR)/kernel/%.o: %.c Makefile | $(BUILD_DIR)/kernel
	$(CC) -c $(CFLAGS) $(KERNEL_DEFS) $(VENDOR_WARN) $< -o $@

$(BUILD_DIR)/vendor/%.o: %.s Makefile | $(BUILD_DIR)/vendor
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile $(LDSCRIPT)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(BIN) $< $@

$(BUILD_DIR)/own $(BUILD_DIR)/vendor $(BUILD_DIR)/kernel:
	mkdir -p $@

clean:
	-rm -rf $(BUILD_DIR)

.PHONY: all clean

-include $(wildcard $(BUILD_DIR)/*/*.d)
