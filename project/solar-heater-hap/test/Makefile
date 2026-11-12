# Host tests of the plain-C logic in main/ (no ESP-IDF): gcc only.
#   make -C project/solar-heater-hap/test
#
#   test_pump_logic   normal mode (sh_pump_logic, step 1)
#   test_pump_ctl     pump test, anti-seize, sensor faults (sh_pump_ctl + sh_pump_logic)
#   test_meas         sensor reading checks and fault (sh_meas)
#   test_settings     menu values: ranges, steps, checks (sh_settings)
#   test_menu         menu transitions, test screen (sh_menu + sh_settings)
#   test_alarms       alarms E1 ... E6 and Hot, their order on the display (sh_alarms, also with sh_pump_ctl)
#   test_led_logic    status LED: colour, pattern, pattern times (sh_led_logic)

CC      := gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O1
MAIN    := ../main
BUILD   := build
TESTS   := test_pump_logic test_pump_ctl test_meas test_settings test_menu test_alarms test_led_logic
HDRS    := $(wildcard $(MAIN)/sh_*.h) test_util.h

.PHONY: test clean

test: $(addprefix $(BUILD)/,$(TESTS))
	@status=0; for t in $^; do ./$$t || status=1; done; exit $$status

$(BUILD)/test_pump_logic: test_pump_logic.c $(MAIN)/sh_pump_logic.c $(HDRS)
$(BUILD)/test_pump_ctl:   test_pump_ctl.c $(MAIN)/sh_pump_ctl.c $(MAIN)/sh_pump_logic.c $(HDRS)
$(BUILD)/test_meas:       test_meas.c $(MAIN)/sh_meas.c $(HDRS)
$(BUILD)/test_settings:   test_settings.c $(MAIN)/sh_settings.c $(HDRS)
$(BUILD)/test_menu:       test_menu.c $(MAIN)/sh_menu.c $(MAIN)/sh_settings.c $(HDRS)
$(BUILD)/test_alarms:     test_alarms.c $(MAIN)/sh_alarms.c $(MAIN)/sh_pump_ctl.c $(MAIN)/sh_pump_logic.c $(HDRS)
$(BUILD)/test_led_logic:  test_led_logic.c $(MAIN)/sh_led_logic.c $(HDRS)

$(BUILD)/%:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ $(filter %.c,$^)

clean:
	rm -rf $(BUILD)
