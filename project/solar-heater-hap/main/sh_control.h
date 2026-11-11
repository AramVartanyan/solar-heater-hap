/* Control loop: every second takes the last published temperatures from sh_state, lets
 * sh_pump decide (normal mode, doc/operating-modes.md 2.1), drives the relay and publishes
 * the pump state with its reason. Parameters: Kconfig "Device Settings" -> "Control". */
#pragma once

#include "esp_err.h"

esp_err_t sh_control_start(void);
