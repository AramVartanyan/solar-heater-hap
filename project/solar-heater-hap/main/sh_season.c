/* Created by Aram Vartanyan (c) 2026 */

/* Seasons: the automatic choice — see sh_season.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_season.h"

static void new_period(sh_season_t *s, uint32_t now_s)
{
    s->period_s = now_s;
    s->have_min = false;
    s->hot = false;
}

bool sh_season_step(sh_season_t *s, sh_season_mode_t mode, bool winter, int16_t t_col, bool pump_on,
                    bool dst_known, bool dst, uint32_t now_s)
{
    /* how long the pump has been running without a break */
    if (!pump_on) {
        s->run = false;
    } else if (!s->run) {
        s->run = true;
        s->run_s = now_s;
    }

    if (mode != SH_SEASON_AUTO || dst_known) {
        /* not by the collector: its observations start over when they are needed again */
        s->started = false;
        s->cold_row = 0;
        s->hot_row = 0;
        return mode == SH_SEASON_AUTO ? !dst : mode == SH_SEASON_WINTER;
    }

    if (!s->started) {
        s->started = true;
        new_period(s, now_s);
    }
    if (t_col != SH_TEMP_INVALID) {
        if (!s->have_min || t_col < s->col_min) {
            s->have_min = true;
            s->col_min = t_col;
        }
        if (s->run && now_s - s->run_s > SH_SEASON_HOT_RUN_S && t_col > SH_SEASON_HOT) {
            s->hot = true;
        }
    }
    if (now_s - s->period_s < SH_SEASON_PERIOD_S) {
        return winter;
    }

    /* a period is over: only the sign that leads out of the current season counts */
    const bool cold = s->have_min && s->col_min < SH_SEASON_COLD;
    s->cold_row = !winter && cold ? (uint8_t)(s->cold_row + 1) : 0;
    s->hot_row = winter && s->hot ? (uint8_t)(s->hot_row + 1) : 0;
    new_period(s, now_s);
    if (s->cold_row >= SH_SEASON_ROW || s->hot_row >= SH_SEASON_ROW) {
        s->cold_row = 0;
        s->hot_row = 0;
        return !winter;
    }
    return winter;
}
