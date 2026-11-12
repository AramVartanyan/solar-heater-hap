/* The modes above the normal control — see sh_modes.h. */

#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_modes.h"

void sh_learned_init(sh_learned_t *l)
{
    memset(l, 0, sizeof(*l));
}

bool sh_modes_winter(const sh_settings_t *set, bool auto_winter)
{
    const int16_t ssn = set->v[SH_SET_SEASON];
    return ssn == SH_SSN_WINTER || (ssn == SH_SSN_AUTO && auto_winter);
}

void sh_modes_init(sh_modes_t *m, bool winter, const sh_learned_t *learned, uint32_t now_s)
{
    memset(m, 0, sizeof(*m));
    sh_sun_init(&m->sun);
    sh_season_init(&m->season, winter);
    m->cool.target = SH_TEMP_INVALID;
    m->cool.last_min = -1;
    if (learned) {
        m->learned = *learned;
    } else {
        sh_learned_init(&m->learned);
    }
    m->period_s = now_s;
    m->target = SH_TEMP_INVALID;
}

void sh_modes_reset_learned(sh_modes_t *m)
{
    sh_learned_init(&m->learned);
    m->changed |= SH_MODES_CH_LEARNED;
}

void sh_modes_step(sh_modes_t *m, const sh_modes_in_t *in, uint32_t now_s)
{
    m->changed = 0;
    const sh_settings_t *set = in->set;

    /* sunrise, sunset */
    const sh_sun_in_t sin = {
        .t_col = in->t_col, .t_tank = in->t_tank, .pump_on = in->pump_on,
        .clock = in->clock_set, .day = in->day, .min = in->min,
    };
    m->sun_evt = sh_sun_step(&m->sun, &sin, now_s);

    /* the day: the local date (a midnight), or 24 h without a clock */
    bool day_over = false;
    if (in->clock_set) {
        if (!m->period_clock) {
            m->period_clock = true;     /* from now on by the date (the part before is dropped) */
        } else if (m->sun_evt & SH_SUN_EVT_DAY) {
            day_over = true;
            sh_sun_list_add(&m->learned.rise, m->sun.done_rise);
            sh_sun_list_add(&m->learned.set, m->sun.done_set);
        }
    } else {
        if (m->period_clock) {
            m->period_clock = false;
            m->period_s = now_s;
        } else if (now_s - m->period_s >= SH_MODES_DAY_S) {
            day_over = true;
            m->period_s = now_s;
        }
    }

    /* the season: by hand, or recognised (Aut) */
    if (set->v[SH_SET_SEASON] == SH_SSN_AUTO) {
        const bool was = m->season.winter;
        if (in->reliable && in->has_dst) {
            sh_season_by_dst(&m->season, in->dst);
        } else {
            sh_season_observe(&m->season, in->t_col, in->charging, now_s);
            if (day_over) {
                sh_season_day(&m->season);
            }
        }
        if (m->season.winter != was) {
            m->changed |= SH_MODES_CH_SEASON;
        }
    }
    m->winter = sh_modes_winter(set, m->season.winter);

    /* the night cooling, the vacation */
    const int16_t tank_max = (int16_t)(set->v[SH_SET_TANK_MAX] * 10);
    m->target = sh_cool_target(in->inst, tank_max, m->learned.cool.offset);
    const sh_cool_in_t cin = {
        .clock = in->reliable, .min = in->min,
        .night = m->sun.night, .sunset_s = m->sun.sunset_s,
        .sunrise = (m->sun_evt & SH_SUN_EVT_RISE) != 0, .sunset = (m->sun_evt & SH_SUN_EVT_SET) != 0,
        .avg_rise = sh_sun_list_avg(&m->learned.rise), .avg_set = sh_sun_list_avg(&m->learned.set),
        .t_col = in->t_col, .t_tank = in->t_tank, .tank_max = in->tank_max,
        .night_cool = set->v[sh_settings_seasonal(SH_SET_NIGHT_COOL, m->winter)] != 0,
        .e4 = in->e4, .vacation = set->v[SH_SET_VACATION] != 0, .target = m->target,
    };
    sh_cool_step(&m->cool, &m->learned.cool, &cin, now_s);
    if (m->cool.events & SH_COOL_EVT_LEARNED) {
        m->changed |= SH_MODES_CH_LEARNED;
    }
    if (day_over) {
        sh_cool_day(&m->cool, &m->learned.cool);
        m->changed |= SH_MODES_CH_LEARNED;
    }
}
