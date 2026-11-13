/* Three front-panel buttons on GPIO (button library: tap, long press, auto-repeat).
 * Short presses drive the user menu and the pump test (sh_menu; doc/operating-modes.md 6.1), choose
 * the network mode (nEt -> sh_net) and confirm an offered firmware update ("FUp");
 * "+" / "-" held > 1 s repeat every 200 ms (the value in the menu).
 * "menu" held and released after CONFIG_SH_NET_RESET_HOLD_S (3 - 7 s, default 5): Wi-Fi network erased;
 * held CONFIG_SH_FACTORY_RESET_HOLD_S (8 - 12 s, default 9): factory reset (everything). Both restart.
 * "+" and "-" held together for 3 s lock / unlock the keyboard (sh_menu; the lock itself is kept by
 * sh_hap — it is the Lock Physical Controls of HomeKit). Locked, no button does anything else. */
#pragma once

#include "esp_err.h"
#include "sh_menu.h"

esp_err_t sh_buttons_init(void);

/* The keyboard lock has changed, from any source (called by sh_hap; any task): "LoC" / "UnL" on
 * the display, an open menu is closed by a lock. */
void sh_buttons_lock_changed(bool locked);

/* What the display shows of the menu now (for the display task). */
void sh_buttons_menu_view(sh_menu_view_t *out);
