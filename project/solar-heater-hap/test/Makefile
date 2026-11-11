# Host test of the pump decision (main/sh_pump.c): plain gcc, no ESP-IDF.
#   make -C project/solar-heater-hap/test

CC      := gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O1
MAIN    := ../main
BIN     := build/test_sh_pump

.PHONY: test clean

test: $(BIN)
	./$(BIN)

$(BIN): test_sh_pump.c $(MAIN)/sh_pump.c $(MAIN)/sh_pump.h $(MAIN)/sh_state.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_sh_pump.c $(MAIN)/sh_pump.c

clean:
	rm -rf build
