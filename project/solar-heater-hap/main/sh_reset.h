/* Resets that end with a restart: from the menu button (sh_buttons) and from the web interface
 * (sh_web). The settings reset without a restart is in sh_settings_store / sh_installer_store. */
#pragma once

#include "esp_err.h"

typedef enum {
    SH_RESET_WIFI,          /* the stored Wi-Fi network only */
    SH_RESET_FACTORY,       /* everything in the "nvs" partition */
} sh_reset_t;

/* Erases and restarts; does not return. */
void sh_reset_now(sh_reset_t what);

/* The same from a task of its own, 0.5 s later: the caller (an HTTP handler) can answer first. */
esp_err_t sh_reset_later(sh_reset_t what);
