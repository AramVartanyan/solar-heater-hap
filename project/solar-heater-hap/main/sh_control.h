/* Control loop: every second takes the last published temperatures from sh_state and the settings
 * in use from sh_settings, lets sh_pump_logic decide (doc/operating-modes.md 2.1), drives the
 * relay, publishes the pump state with its reason and logs every change of either, and the start
 * and the end of the pump test and of the anti-seize run.
 * Service parameters: Kconfig "Device Settings" -> "Control". */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sh_menu.h"

esp_err_t sh_control_start(void);

/* A short key press on the normal screen: "+" starts the pump test or adds time to it, "-" and
 * "menu" act on a running test. The pump follows at once. Returns true when the key was taken by
 * the pump test, false when it is free for the menu. */
bool sh_control_key(sh_key_t key);
