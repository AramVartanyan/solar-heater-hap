/* Installer data in the "nvs" partition (namespace "sh_installer"): read at boot, a missing or
 * invalid value -> its default (Kconfig "Installer data"); written only when a value changes. The
 * computed limits (sh_installer) are kept with them and change at once with the data; every change
 * logs the limits and the collector protection in use (the Kconfig setting, capped).
 * Table, checks and computations: sh_installer. */
#pragma once

#include "esp_err.h"
#include "sh_installer.h"

esp_err_t sh_installer_init(void);                      /* after nvs_flash_init() */
void sh_installer_get(sh_installer_t *data, sh_limits_t *limits);   /* consistent copy; NULL: skip */
esp_err_t sh_installer_set(sh_inst_id_t id, int16_t value);   /* checked; no write if unchanged */
esp_err_t sh_installer_reset(void);     /* erases the namespace: the defaults, at once */

/* Collector protection setting (Kconfig), tenths of °C, and the one in use (capped). */
int16_t sh_installer_col_protect(void);
int16_t sh_installer_col_protect_used(const sh_limits_t *limits);
