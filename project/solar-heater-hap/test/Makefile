# Host test of the pump decision (no ESP-IDF): make -C project/solar-heater-hap/test
CC      := gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
MAIN    := ../main

test_pump_logic: test_pump_logic.c $(MAIN)/sh_pump_logic.c $(MAIN)/sh_pump_logic.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_pump_logic.c $(MAIN)/sh_pump_logic.c

.PHONY: run clean
run: test_pump_logic
	./test_pump_logic

clean:
	rm -f test_pump_logic

.DEFAULT_GOAL := run
