/* Three front-panel buttons on GPIO (button library: tap, long press, auto-repeat).
 * "menu" held and released after CONFIG_SH_NET_RESET_HOLD_S (3 - 7 s, default 5): Wi-Fi network erased;
 * held CONFIG_SH_FACTORY_RESET_HOLD_S (8 - 12 s, default 9): factory reset (everything). Both restart. */
#pragma once

#include "esp_err.h"

esp_err_t sh_buttons_init(void);
