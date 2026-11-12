/* Control loop: every second takes the last published measurement from sh_state and the current
 * settings (sh_settings_store), lets sh_pump_ctl decide (pump test, anti-seize, sensor faults,
 * normal mode — doc/operating-modes.md 2.1), drives the relay, publishes the pump state with its
 * reason and the pump test, and logs every change of the pump or the reason, the start / end of
 * the test and of the anti-seize run. Then the alarm and information codes (sh_alarm): published,
 * every appearance and clearing logged; and the status LED (sh_ind -> sh_led), computed when its
 * inputs change. Fixed parameters: Kconfig "Device Settings" -> "Control"; the boiling point, the
 * ceiling of the collector protection and the frost threshold come from the installer data
 * (sh_installer_store) and are logged at boot and when they change.
 * Group C (doc/tasks/step10-cooling-vacation-seasons.md): before the pump decision — sunrise and
 * sunset (sh_sun), the season (sh_season; the seasonal values of sh_settings), the night cooling
 * sessions (sh_cool) and the vacation (collector protection at the vacation limit, no charging);
 * the air venting is a command like the pump test. The learned values are kept by sh_learn_store.
 * Group D (doc/tasks/step12-heater-leak-e8.md): before the pump decision — the heater sign and the
 * heater found with "0 kW" (sh_heater), the checks for its heat leaking to the collector (they stop
 * the pump that charges, or keep it off); after it — the nights of the reverse thermosiphon
 * (sh_e8), whose E8 goes to the alarm codes. */
#pragma once

#include "esp_err.h"
#include "sdkconfig.h"

/* Collector protection setting (Kconfig), tenths of °C; limited by the computed ceiling. */
#define SH_CONTROL_COL_PROTECT      (CONFIG_SH_COL_PROTECT_C * 10)

/* Pump test commands from the buttons (normal screen), applied at once by the control task. */
typedef enum {
    SH_CONTROL_TEST_PLUS,       /* start the test, or +60 s */
    SH_CONTROL_TEST_MINUS,      /* -60 s */
    SH_CONTROL_TEST_STOP,       /* stop (not earlier than 1 min after the start) */
    SH_CONTROL_AIR_START,       /* air venting (web interface) */
    SH_CONTROL_AIR_STOP,        /* its stop (web interface, short "menu") */
    SH_CONTROL_RESET_LEARNED,   /* forget the learned values (sunrise / sunset, cooling target, heater, E8) */
} sh_control_cmd_t;

esp_err_t sh_control_start(void);
void sh_control_cmd(sh_control_cmd_t cmd);
