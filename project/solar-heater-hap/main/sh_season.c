/* Seasons — see sh_season.h. */

#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_season.h"

static void new_day(sh_season_t *s)
{
    s->col_min = SH_TEMP_INVALID;
    s->hot = false;
}

void sh_season_init(sh_season_t *s, bool winter)
{
    memset(s, 0, sizeof(*s));
    s->winter = winter;
    new_day(s);
}

void sh_season_observe(sh_season_t *s, int16_t t_col, bool charging, uint32_t now_s)
{
    if (charging && !s->charging) {
        s->charge_s = now_s;
    }
    s->charging = charging;
    if (t_col == SH_TEMP_INVALID) {
        return;
    }
    if (s->col_min == SH_TEMP_INVALID || t_col < s->col_min) {
        s->col_min = t_col;
    }
    if (charging && t_col > SH_SEASON_HOT && now_s - s->charge_s > SH_SEASON_HOT_RUN_S) {
        s->hot = true;
    }
}

static void change(sh_season_t *s, bool winter)
{
    s->winter = winter;
    s->cold_row = 0;
    s->hot_row = 0;
}

bool sh_season_day(sh_season_t *s)
{
    bool changed = false;
    if (s->col_min != SH_TEMP_INVALID) {
        if (!s->winter) {
            s->cold_row = s->col_min < SH_SEASON_COLD ? (uint8_t)(s->cold_row + 1) : 0;
            if (s->cold_row >= SH_SEASON_DAYS) {
                change(s, true);
                changed = true;
            }
        } else {
            s->hot_row = s->hot ? (uint8_t)(s->hot_row + 1) : 0;
            if (s->hot_row >= SH_SEASON_DAYS) {
                change(s, false);
                changed = true;
            }
        }
    }
    new_day(s);
    return changed;
}

bool sh_season_by_dst(sh_season_t *s, bool dst)
{
    if (s->winter != !dst) {
        change(s, !dst);
        return true;
    }
    return false;
}
