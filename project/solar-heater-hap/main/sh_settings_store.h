/* User settings in the "nvs" partition (namespace "sh_settings"): read at boot, a missing or
 * invalid value -> its default (Kconfig "Control"); written only when a value changes. The factory
 * reset erases the partition, so the defaults come back. Table and checks: sh_settings.
 * Besides the menu values: the frost protection on / off (key "frost", web interface only). */
#pragma once

#include "esp_err.h"
#include "sh_settings.h"

esp_err_t sh_settings_init(void);                       /* after nvs_flash_init() */
void sh_settings_get(sh_settings_t *out);               /* consistent copy */
esp_err_t sh_settings_set(sh_setting_t id, int16_t value);  /* checked; no write if unchanged */
bool sh_settings_frost_get(void);
esp_err_t sh_settings_frost_set(bool on);                    /* no write if unchanged */
esp_err_t sh_settings_reset(void);      /* erases the namespace: the defaults, at once */
