/* Created by Aram Vartanyan (c) 2026 */

/* User settings (the button menu, doc/operating-modes.md 6.1; the seasons, 2.2): table, ranges
 * and checks — plain C, compiled into the host test too. Storage in NVS: sh_settings_store.
 *
 * Whole units, as shown on the display: °C, K, s. The switch-off difference is always below the
 * switch-on difference: in the menu each one's range is narrowed by the other; a stored pair that
 * breaks the rule falls back to the defaults (both).
 *
 * Seasons: C.Lo, d.on and d.oF and the night cooling above Tmax have two sets, summer and winter.
 * The menu shows and changes the set of the current season, the web interface both. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SH_SET_TANK_MAX = 0,        /* tbo:  tank Tmax, 30 - 85 °C */
    SH_SET_COL_MIN,             /* C.Lo: minimum collector temperature, 10 - 80 °C (summer) */
    SH_SET_DIFF_ON,             /* d.on: switch-on difference, 3 - 20 K (summer) */
    SH_SET_DIFF_OFF,            /* d.oF: switch-off difference, 1 - 15 K (summer) */
    SH_SET_TEST_S,              /* tSt:  pump test time, 60 - 900 s, step 30 s */
    SH_SET_COL_MIN_W,           /* C.Lo, winter */
    SH_SET_DIFF_ON_W,           /* d.on, winter */
    SH_SET_DIFF_OFF_W,          /* d.oF, winter */
    SH_SET_SEASON,              /* SSn:  sh_season_mode_t */
    SH_SET_VACATION,            /* HoL:  vacation, 0 / 1 */
    SH_SET_NCOOL,               /* night cooling above Tmax, 0 / 1 (summer); web interface only */
    SH_SET_NCOOL_W,             /* ... winter */
    SH_SET_COUNT
} sh_setting_t;

/* SSn */
typedef enum {
    SH_SEASON_SUMMER = 0,       /* Sun */
    SH_SEASON_WINTER,           /* Cld */
    SH_SEASON_AUTO,             /* Aut */
    SH_SEASON_MODES
} sh_season_mode_t;

typedef struct {
    int16_t v[SH_SET_COUNT];
} sh_settings_t;

typedef struct {
    const char *key;            /* NVS key */
    int16_t min, max, step;
} sh_setting_info_t;

extern const sh_setting_info_t sh_setting_info[SH_SET_COUNT];

/* The setting that holds `id` in the given season: the winter twin of a seasonal summer value
 * when `winter`, otherwise `id` itself. */
sh_setting_t sh_settings_seasonal(sh_setting_t id, bool winter);

/* Allowed range of `id` with the other values of `s` (d.on >= d.oF + 1, d.oF <= d.on - 1, each
 * season on its own). */
void sh_settings_range(const sh_settings_t *s, sh_setting_t id, int16_t *min, int16_t *max);

/* `v` is in the table range of `id` and on its step. */
bool sh_settings_value_ok(sh_setting_t id, int16_t v);

/* Values read from storage: each one out of range (or off the step) -> its default; a switch-off
 * difference not below the switch-on difference -> both defaults. Returns a bit mask of the
 * replaced values (bit = sh_setting_t). */
unsigned sh_settings_sanitize(sh_settings_t *s, const sh_settings_t *defaults);

/* `v` moved one step up (`dir` > 0) or down, held in the allowed range (no wrap). */
int16_t sh_settings_step(const sh_settings_t *s, sh_setting_t id, int16_t v, int dir);
