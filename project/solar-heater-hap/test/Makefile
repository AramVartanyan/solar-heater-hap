# Host tests of the logic in plain C (main/sh_pump_logic.c, sh_sensor_logic.c, sh_settings_logic.c,
# sh_menu.c): plain gcc, no ESP-IDF.
#   make -C project/solar-heater-hap/test

CC      := gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O1
MAIN    := ../main
TESTS   := test_pump_logic test_sensor_logic test_settings_menu
BINS    := $(addprefix build/,$(TESTS))

.PHONY: test clean

test: $(BINS)
	@for t in $(BINS); do ./$$t || exit 1; done

build/test_pump_logic: test_pump_logic.c $(MAIN)/sh_pump_logic.c
build/test_sensor_logic: test_sensor_logic.c $(MAIN)/sh_sensor_logic.c
build/test_settings_menu: test_settings_menu.c $(MAIN)/sh_settings_logic.c $(MAIN)/sh_menu.c

$(BINS): test_util.h $(wildcard $(MAIN)/sh_*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ $(filter %.c,$^)

clean:
	rm -rf build
