/* User settings (the button menu, doc/operating-modes.md 2.5; the seasons 2.1.2, 2.2): table, ranges
 * and checks — plain C, compiled into the host test too. Storage in NVS: sh_settings_store.
 *
 * Whole units, as shown on the display: °C, K, s; the season mode and the switches as numbers.
 * The first SH_SET_MENU_COUNT values are the items of the button menu, in its order (SSn, HoL,
 * tbo, C.Lo, d.on, d.oF, tSt); the rest only in the web interface. C.Lo, d.on, d.oF and the night
 * cooling above Tmax have a summer and a winter value: the menu shows and changes the one of the
 * current season (sh_settings_seasonal), the web interface both. In each season the switch-off
 * difference is below the switch-on difference: each one's range is narrowed by the other; a
 * stored pair that breaks the rule falls back to the defaults (both). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SH_SET_SEASON = 0,          /* SSn:  season mode, sh_ssn_t (Sun / Cld / Aut) */
    SH_SET_VACATION,            /* HoL:  vacation, 0 / 1 */
    SH_SET_TANK_MAX,            /* tbo:  tank Tmax, 30 - 85 °C */
    SH_SET_COL_MIN,             /* C.Lo: minimum collector temperature, summer, 10 - 80 °C */
    SH_SET_DIFF_ON,             /* d.on: switch-on difference, summer, 3 - 20 K */
    SH_SET_DIFF_OFF,            /* d.oF: switch-off difference, summer, 1 - 15 K */
    SH_SET_TEST_S,              /* tSt:  pump test time, 60 - 900 s, step 30 s */
    SH_SET_COL_MIN_W,           /* the same three, winter */
    SH_SET_DIFF_ON_W,
    SH_SET_DIFF_OFF_W,
    SH_SET_NIGHT_COOL,          /* night cooling above Tmax (trigger a), summer, 0 / 1 */
    SH_SET_NIGHT_COOL_W,        /* ... winter */
    SH_SET_COUNT
} sh_setting_t;

#define SH_SET_MENU_COUNT       (SH_SET_TEST_S + 1)

/* SSn */
typedef enum {
    SH_SSN_SUMMER = 0,          /* Sun: summer, set by hand */
    SH_SSN_WINTER,              /* Cld: winter, set by hand */
    SH_SSN_AUTO,                /* Aut: recognised by the controller */
    SH_SSN_COUNT
} sh_ssn_t;

typedef struct {
    int16_t v[SH_SET_COUNT];
} sh_settings_t;

typedef struct {
    const char *key;            /* NVS key, also the name in the web interface */
    int16_t min, max, step;
} sh_setting_info_t;

extern const sh_setting_info_t sh_setting_info[SH_SET_COUNT];

/* The value of `id` that applies in the season (`winter`): the winter twin of C.Lo, d.on, d.oF and
 * of the night cooling; any other `id` as it is. */
sh_setting_t sh_settings_seasonal(sh_setting_t id, bool winter);

/* Allowed range of `id` with the other values of `s` (d.on >= d.oF + 1, d.oF <= d.on - 1, in each
 * season). */
void sh_settings_range(const sh_settings_t *s, sh_setting_t id, int16_t *min, int16_t *max);

/* `v` is in the table range of `id` and on its step. */
bool sh_settings_value_ok(sh_setting_t id, int16_t v);

/* Values read from storage: each one out of range (or off the step) -> its default; a switch-off
 * difference not below the switch-on difference -> both defaults (of that season). Returns a bit
 * mask of the replaced values (bit = sh_setting_t). */
unsigned sh_settings_sanitize(sh_settings_t *s, const sh_settings_t *defaults);

/* `v` moved one step up (`dir` > 0) or down, held in the allowed range (no wrap). */
int16_t sh_settings_step(const sh_settings_t *s, sh_setting_t id, int16_t v, int dir);
