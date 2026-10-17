##############################################################################
# tests/Makefile — хостові тести (звичайний gcc, без плати).
#   make -C tests          — зібрати й запустити всі тести
#   make -C tests coverage — те саме з gcov + звіт gcovr для Devices/ і App/
#
# Фреймворк: Unity v2.6.1 (tests/vendor/unity, коміт cbcd08f).
# Test doubles: tests/doubles/ (fake I2C, mock SPI, fake SPI-flash, fake time, fake app_hw).
##############################################################################

CC       ?= gcc
FW        = ../firmware
BUILD     = build
COV_MIN  ?= 90   # мінімальне покриття рядків Devices/+App/, % (обґрунтування — docs/lab2-report.md)

SRC_DEV  = $(wildcard $(FW)/Devices/*.c)
SRC_APP  = $(wildcard $(FW)/App/*.c)
SRC_DBL  = $(wildcard doubles/*.c)
UNITY    = vendor/unity/unity.c

TESTS    = $(patsubst %.c,%,$(wildcard test_*.c))

CFLAGS   = -std=gnu11 -O0 -g -Wall -Wextra -Werror -Wshadow \
           -I$(FW)/Devices -I$(FW)/App -Idoubles -Ivendor/unity -Ivectors \
           -DUNITY_INCLUDE_DOUBLE
LDLIBS   = -lm

ifeq ($(COVERAGE),1)
CFLAGS  += --coverage -fprofile-update=atomic
endif

.PHONY: all run coverage clean

all: run

$(BUILD):
	mkdir -p $@

$(BUILD)/%: %.c $(SRC_DEV) $(SRC_APP) $(SRC_DBL) $(UNITY) $(wildcard vectors/*.h doubles/*.h $(FW)/Devices/*.h $(FW)/App/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $< $(SRC_DEV) $(SRC_APP) $(SRC_DBL) $(UNITY) -o $@ $(LDLIBS)

run: $(addprefix $(BUILD)/,$(TESTS))
	@fail=0; for t in $^; do echo "=== $$t"; ./$$t || fail=1; done; exit $$fail

coverage:
	rm -rf $(BUILD)
	$(MAKE) COVERAGE=1 run
	gcovr --root $(FW) --object-directory . --filter '$(FW)/Devices/' --filter '$(FW)/App/' \
	      --print-summary --txt $(BUILD)/coverage.txt --fail-under-line $(strip $(COV_MIN)) .
	@cat $(BUILD)/coverage.txt

clean:
	rm -rf $(BUILD) *.gcda *.gcno
