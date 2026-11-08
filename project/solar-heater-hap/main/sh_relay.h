/* Pump relay: GPIO -> transistor -> relay coil (pull-down keeps it off at reset). */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t sh_relay_init(void);      /* relay off */
void sh_relay_set(bool on);
bool sh_relay_get(void);
