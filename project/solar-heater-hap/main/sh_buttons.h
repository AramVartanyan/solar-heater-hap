/* Three front-panel buttons on GPIO (button library: tap, long press, auto-repeat). */
#pragma once

#include "esp_err.h"

esp_err_t sh_buttons_init(void);
