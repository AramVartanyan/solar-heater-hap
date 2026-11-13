/* Created by Aram Vartanyan (c) 2026 */

/* Control loop: every second takes the last published measurement from sh_state and the current
 * settings (sh_settings_store), lets sh_pump_ctl decide (pump test, anti-seize, sensor faults,
 * normal mode — doc/operating-modes.md 2 - 4), drives the relay, publishes the pump state with its
 * reason and the pump test, and logs every change of the pump or the reason, the start / end of
 * the test and of the anti-seize run. Then the alarm and information codes (sh_alarm): published,
 * every appearance and clearing logged; and the status LED (sh_ind -> sh_led), computed when its
 * inputs change. Fixed parameters: Kconfig "Device Settings" -> "Control"; the boiling point, the
 * ceiling of the collector protection and the frost threshold come from the installer data
 * (sh_installer_store) and are logged at boot and when they change.
 * Before the pump decision: sunrise and
 * sunset (sh_sun), the season (sh_season; the seasonal values of sh_settings), the night cooling
 * sessions (sh_cool) and the vacation (collector protection at the vacation limit, no charging);
 * the air venting is a command like the pump test. The learned values are kept by sh_learn_store. */
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
    SH_CONTROL_RESET_LEARNED,   /* forget the learned sunrise / sunset and cooling target */
} sh_control_cmd_t;

esp_err_t sh_control_start(void);
void sh_control_cmd(sh_control_cmd_t cmd);
