/* Created by Aram Vartanyan (c) 2026 */

/* Installer data in the "nvs" partition (namespace "sh_installer"): read at boot, a missing or
 * out-of-range value -> its default (Kconfig "Installer data"); written only when a value changes.
 * Changed from the "Service" section of the web interface; a change applies at once (the control
 * task reads the data and the computed limits on every cycle). Checks and limits: sh_installer. */
#pragma once

#include "esp_err.h"
#include "sh_installer.h"

esp_err_t sh_installer_init(void);                      /* after nvs_flash_init() */
/* Consistent copy of the data and of the limits computed from them (either may be NULL). */
void sh_installer_get(sh_installer_t *data, sh_limits_t *limits);
esp_err_t sh_installer_set(const sh_installer_t *data); /* checked; writes the changed values */
esp_err_t sh_installer_reset(void);                     /* erases the namespace: the defaults, at once */
