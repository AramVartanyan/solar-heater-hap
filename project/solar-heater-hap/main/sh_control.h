/* Control loop: every second takes the last published temperatures from sh_state, lets
 * sh_pump_logic decide (normal mode, doc/operating-modes.md 2.1), drives the relay, publishes
 * the pump state with its reason and logs every change of either.
 * Parameters: Kconfig "Device Settings" -> "Control". */
#pragma once

#include "esp_err.h"

esp_err_t sh_control_start(void);
