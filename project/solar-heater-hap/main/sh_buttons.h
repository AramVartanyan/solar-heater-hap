/* Three front-panel buttons on GPIO (button library: tap, long press, auto-repeat).
 * Temporary: holding "menu" CONFIG_SH_NET_RESET_HOLD_S (3 - 7 s, default 5) erases the Wi-Fi network and restarts. */
#pragma once

#include "esp_err.h"

esp_err_t sh_buttons_init(void);
