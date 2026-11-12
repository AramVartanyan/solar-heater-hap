/* User settings in use, kept in the "nvs" partition (namespace "sh_settings"): read at boot, a
 * missing or bad value becomes its default (Kconfig "Device Settings" -> "Control"), written only
 * when a value changes. The factory reset erases the partition, so it brings the defaults back.
 * The list, the ranges and the checks: sh_settings_logic. */
#pragma once

#include "esp_err.h"
#include "sh_settings_logic.h"

esp_err_t sh_settings_init(void);               /* after nvs_flash_init() */
void sh_settings_get(sh_settings_t *out);       /* consistent copy */

/* Changes one setting; the new value is in use at once. Writes to NVS only when the value differs
 * from the one in use. ESP_ERR_INVALID_ARG: outside the range the setting can have right now. */
esp_err_t sh_settings_set(sh_setting_id_t id, int16_t value);
