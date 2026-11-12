/* Three front-panel buttons on GPIO (button library: tap, long press, auto-repeat).
 * Short presses of "menu", "+" and "-" go to the display task (menu, pump test: sh_display_key);
 * "+" / "-" held longer than 1 s repeat every 200 ms (for the values in the menu).
 * "menu" held and released after CONFIG_SH_NET_RESET_HOLD_S (3 - 7 s, default 5): Wi-Fi network erased;
 * held CONFIG_SH_FACTORY_RESET_HOLD_S (8 - 12 s, default 9): factory reset (everything). Both restart. */
#pragma once

#include "esp_err.h"

esp_err_t sh_buttons_init(void);
