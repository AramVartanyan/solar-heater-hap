/* Three front-panel buttons on GPIO (button library: tap, long press, auto-repeat).
 * Short presses drive the user menu and the pump test (sh_menu; doc/operating-modes.md 2.5);
 * "+" / "-" held > 1 s repeat every 200 ms (the value in the menu).
 * "menu" held and released after CONFIG_SH_NET_RESET_HOLD_S (3 - 7 s, default 5): Wi-Fi network erased;
 * held CONFIG_SH_FACTORY_RESET_HOLD_S (8 - 12 s, default 9): factory reset (everything). Both restart. */
#pragma once

#include "esp_err.h"
#include "sh_menu.h"

esp_err_t sh_buttons_init(void);

/* What the display shows of the menu now (for the display task). */
void sh_buttons_menu_view(sh_menu_view_t *out);
