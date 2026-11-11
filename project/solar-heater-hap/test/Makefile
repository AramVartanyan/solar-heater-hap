# Host test of the pump decision (main/sh_pump_logic.c): plain gcc, no ESP-IDF.
#   make -C project/solar-heater-hap/test

CC      := gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O1
MAIN    := ../main
BIN     := build/test_pump_logic

.PHONY: test clean

test: $(BIN)
	./$(BIN)

$(BIN): test_pump_logic.c $(MAIN)/sh_pump_logic.c $(MAIN)/sh_pump_logic.h $(MAIN)/sh_state.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_pump_logic.c $(MAIN)/sh_pump_logic.c

clean:
	rm -rf build
