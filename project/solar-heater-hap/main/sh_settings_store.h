/* User settings in the "nvs" partition (namespace "sh_settings"): read at boot, a missing or
 * invalid value -> its default (Kconfig "Control"); written only when a value changes. The factory
 * reset erases the partition, so the defaults come back. Table and checks: sh_settings.
 * The frost protection switch (web interface only, key "fro") and the current season (key
 * "ssn_now": what the automatic mode has chosen, or the season set by hand) are kept here too; from
 * group F the keypad lock ("lock", the same as Lock Physical Controls of HomeKit) and the display
 * units ("unit": 0 °C, 1 °F — set only from HomeKit). "Default settings" erases the namespace: the
 * keypad is unlocked and the units are °C again (sh_hap notifies HomeKit of the changes). */
#pragma once

#include "esp_err.h"
#include "sh_settings.h"

esp_err_t sh_settings_init(void);                       /* after nvs_flash_init() */
void sh_settings_get(sh_settings_t *out);               /* consistent copy */
esp_err_t sh_settings_set(sh_setting_t id, int16_t value);  /* checked; no write if unchanged */
esp_err_t sh_settings_put(const sh_settings_t *s);      /* all values, checked; writes the changed ones */
bool sh_settings_frost(void);                           /* frost protection enabled */
esp_err_t sh_settings_set_frost(bool on);               /* no write if unchanged */
bool sh_settings_winter(void);                          /* the current season is winter */
esp_err_t sh_settings_set_winter(bool winter);          /* no write if unchanged */
bool sh_settings_locked(void);                          /* the keypad is locked */
esp_err_t sh_settings_set_locked(bool on);              /* no write if unchanged */
bool sh_settings_fahrenheit(void);                      /* the display units are °F */
esp_err_t sh_settings_set_fahrenheit(bool on);          /* no write if unchanged */
esp_err_t sh_settings_reset(void);                      /* erases the namespace: the defaults, at once */
