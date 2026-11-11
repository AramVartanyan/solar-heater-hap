/* Control loop (1 s): reads the latest temperatures from sh_state, decides the pump
 * (sh_pump_logic), drives the relay, publishes pump state and reason; logs only on a switch. */
#pragma once

#include "esp_err.h"

esp_err_t sh_control_start(void);
