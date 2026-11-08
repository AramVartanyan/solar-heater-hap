/* TM1637, 3 digits (doc/hardware.md). Phase 1: stub — esp_tm1637 gets the agreed
 * updates (ESP-IDF 5.x, ACK timeout, 1 - 6 digits, open-drain) before it is used. */
#pragma once

#include "esp_err.h"

esp_err_t sh_display_init(void);
