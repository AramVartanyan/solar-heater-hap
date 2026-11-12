/* User settings: the list, the ranges and the checks (doc/operating-modes.md 2.5) — plain C, no
 * ESP-IDF / FreeRTOS, compiled into the host test too. Storage: sh_settings.
 *
 * Values are whole units, as the menu shows them (°C, K, s). The switch-off difference is always
 * lower than the switch-on difference: in the menu the range of each is narrowed by the other, and
 * a stored pair that breaks the rule is replaced by the defaults. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SH_SET_TANK_MAX = 0,        /* "tbo":  tank Tmax, 30 - 85 °C */
    SH_SET_COL_MIN,             /* "C.Lo": minimum collector temperature, 10 - 80 °C */
    SH_SET_DIFF_ON,             /* "d.on": switch-on difference, 3 - 20 K */
    SH_SET_DIFF_OFF,            /* "d.oF": switch-off difference, 1 - 15 K */
    SH_SET_TEST_S,              /* "tSt":  pump test time, 60 - 900 s, step 30 */
    SH_SET_COUNT
} sh_setting_id_t;

typedef struct {
    int16_t v[SH_SET_COUNT];
} sh_settings_t;

typedef struct {
    int16_t min;
    int16_t max;
    int16_t step;
} sh_setting_range_t;

/* The fixed range and step of a setting. */
sh_setting_range_t sh_setting_limits(sh_setting_id_t id);

/* The range a setting can be changed in right now: the fixed one, for the two differences narrowed
 * by the other one (on >= off + 1, off <= on - 1). */
sh_setting_range_t sh_setting_range(const sh_settings_t *s, sh_setting_id_t id);

/* Stored values -> values in use. `stored` holds what was read; bit `id` of `present` tells that
 * the value exists. A missing value or one out of its fixed range becomes the default; if the two
 * differences then break their rule, both become the defaults. */
void sh_settings_check(sh_settings_t *out, const sh_settings_t *stored, uint32_t present,
                       const sh_settings_t *defaults);
