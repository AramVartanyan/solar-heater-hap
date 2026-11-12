# Host tests of the plain-C logic in main/ (no ESP-IDF): gcc only.
#   make -C project/solar-heater-hap/test
#
#   test_pump_logic   normal mode (sh_pump_logic, step 1)
#   test_pump_ctl     pump test, anti-seize, sensor faults (sh_pump_ctl + sh_pump_logic)
#   test_meas         sensor reading checks and fault (sh_meas)
#   test_settings     menu values: ranges, steps, checks (sh_settings)
#   test_menu         menu transitions, test screen (sh_menu + sh_settings)
#   test_alarm        alarm codes, E6, display order (sh_alarm + sh_pump_ctl + sh_pump_logic)
#   test_ind          status LED: colour, pattern, times (sh_ind)
#   test_installer    installer data, boiling point, limits (sh_installer)
#   test_web          web interface data and requests (sh_web_api)
#   test_clock_history  clock rules, time zones, the day in RAM, daily records (sh_clock_rules,
#                     sh_tz, sh_day, sh_hist)
#   test_group_c      night cooling, sunrise / sunset, vacation, seasons, air venting (sh_cool,
#                     sh_sun, sh_season, sh_pump_logic, sh_pump_ctl, sh_installer, sh_ind, sh_day)
#   test_group_d      heater sign and power, heater leak (daylight, check schedule, check result),
#                     reverse thermosiphon E8 (sh_heater, sh_e8, sh_pump_logic, sh_pump_ctl, sh_sun,
#                     sh_alarm, sh_ind, sh_installer, sh_day, sh_hist)

CC      := gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O1
MAIN    := ../main
BUILD   := build
TESTS   := test_pump_logic test_pump_ctl test_meas test_settings test_menu test_alarm test_ind \
           test_installer test_web test_clock_history test_group_c test_group_d
HIST_SRC := $(MAIN)/sh_clock_rules.c $(MAIN)/sh_tz.c $(MAIN)/sh_day.c $(MAIN)/sh_hist.c $(MAIN)/sh_alarm.c
WEB_SRC := $(MAIN)/sh_web_api.c $(MAIN)/sh_installer.c $(MAIN)/sh_settings.c $(MAIN)/sh_pump_logic.c \
           $(MAIN)/sh_heater.c $(HIST_SRC)
GROUP_C_SRC := $(MAIN)/sh_cool.c $(MAIN)/sh_sun.c $(MAIN)/sh_season.c
HDRS    := $(wildcard $(MAIN)/sh_*.h) test_util.h

.PHONY: test sim clean

test: $(addprefix $(BUILD)/,$(TESTS))
	@status=0; for t in $^; do ./$$t || status=1; done; exit $$status

$(BUILD)/test_pump_logic: test_pump_logic.c $(MAIN)/sh_pump_logic.c $(HDRS)
$(BUILD)/test_pump_ctl:   test_pump_ctl.c $(MAIN)/sh_pump_ctl.c $(MAIN)/sh_pump_logic.c $(HDRS)
$(BUILD)/test_meas:       test_meas.c $(MAIN)/sh_meas.c $(HDRS)
$(BUILD)/test_settings:   test_settings.c $(MAIN)/sh_settings.c $(HDRS)
$(BUILD)/test_menu:       test_menu.c $(MAIN)/sh_menu.c $(MAIN)/sh_settings.c $(HDRS)
$(BUILD)/test_alarm:      test_alarm.c $(MAIN)/sh_alarm.c $(MAIN)/sh_pump_ctl.c $(MAIN)/sh_pump_logic.c $(HDRS)
$(BUILD)/test_ind:        test_ind.c $(MAIN)/sh_ind.c $(HDRS)
$(BUILD)/test_installer:  test_installer.c $(MAIN)/sh_installer.c $(HDRS)
$(BUILD)/test_web:        test_web.c $(WEB_SRC) $(HDRS)
$(BUILD)/test_clock_history: test_clock_history.c $(HIST_SRC) $(MAIN)/sh_pump_logic.c $(HDRS)
$(BUILD)/test_group_c:    test_group_c.c $(GROUP_C_SRC) $(MAIN)/sh_pump_ctl.c $(MAIN)/sh_pump_logic.c \
                          $(MAIN)/sh_installer.c $(MAIN)/sh_ind.c $(HIST_SRC) $(HDRS)
$(BUILD)/test_group_d:    test_group_d.c $(MAIN)/sh_heater.c $(MAIN)/sh_e8.c $(MAIN)/sh_sun.c $(MAIN)/sh_pump_ctl.c \
                          $(MAIN)/sh_pump_logic.c $(MAIN)/sh_installer.c $(MAIN)/sh_ind.c $(HIST_SRC) $(HDRS)
$(BUILD)/web_sim:         web_sim.c $(WEB_SRC) $(GROUP_C_SRC) $(MAIN)/sh_e8.c $(MAIN)/sh_pump_ctl.c $(HDRS)

$(BUILD)/%:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ $(filter %.c,$^) -lm

sim: $(BUILD)/web_sim
	./$< $(MAIN)/web/index.html

clean:
	rm -rf $(BUILD)
