/* User settings: table, ranges and checks — see sh_settings.h. */

#include "sh_settings.h"

const sh_setting_info_t sh_setting_info[SH_SET_COUNT] = {
    [SH_SET_SEASON]       = { "ssn",     0, SH_SSN_COUNT - 1, 1 },
    [SH_SET_VACATION]     = { "hol",     0,   1,  1 },
    [SH_SET_TANK_MAX]     = { "tbo",    30,  85,  1 },
    [SH_SET_COL_MIN]      = { "c_lo",   10,  80,  1 },
    [SH_SET_DIFF_ON]      = { "d_on",    3,  20,  1 },
    [SH_SET_DIFF_OFF]     = { "d_of",    1,  15,  1 },
    [SH_SET_TEST_S]       = { "t_st",   60, 900, 30 },
    [SH_SET_COL_MIN_W]    = { "c_lo_w", 10,  80,  1 },
    [SH_SET_DIFF_ON_W]    = { "d_on_w",  3,  20,  1 },
    [SH_SET_DIFF_OFF_W]   = { "d_of_w",  1,  15,  1 },
    [SH_SET_NIGHT_COOL]   = { "nc",      0,   1,  1 },
    [SH_SET_NIGHT_COOL_W] = { "nc_w",    0,   1,  1 },
};

sh_setting_t sh_settings_seasonal(sh_setting_t id, bool winter)
{
    if (!winter) {
        return id;
    }
    switch (id) {
    case SH_SET_COL_MIN:
        return SH_SET_COL_MIN_W;
    case SH_SET_DIFF_ON:
        return SH_SET_DIFF_ON_W;
    case SH_SET_DIFF_OFF:
        return SH_SET_DIFF_OFF_W;
    case SH_SET_NIGHT_COOL:
        return SH_SET_NIGHT_COOL_W;
    default:
        return id;
    }
}

void sh_settings_range(const sh_settings_t *s, sh_setting_t id, int16_t *min, int16_t *max)
{
    *min = sh_setting_info[id].min;
    *max = sh_setting_info[id].max;
    if (id == SH_SET_DIFF_ON && s->v[SH_SET_DIFF_OFF] + 1 > *min) {
        *min = (int16_t)(s->v[SH_SET_DIFF_OFF] + 1);
    } else if (id == SH_SET_DIFF_OFF && s->v[SH_SET_DIFF_ON] - 1 < *max) {
        *max = (int16_t)(s->v[SH_SET_DIFF_ON] - 1);
    } else if (id == SH_SET_DIFF_ON_W && s->v[SH_SET_DIFF_OFF_W] + 1 > *min) {
        *min = (int16_t)(s->v[SH_SET_DIFF_OFF_W] + 1);
    } else if (id == SH_SET_DIFF_OFF_W && s->v[SH_SET_DIFF_ON_W] - 1 < *max) {
        *max = (int16_t)(s->v[SH_SET_DIFF_ON_W] - 1);
    }
}

bool sh_settings_value_ok(sh_setting_t id, int16_t v)
{
    const sh_setting_info_t *i = &sh_setting_info[id];
    return v >= i->min && v <= i->max && (v - i->min) % i->step == 0;
}

/* A pair that breaks the rule: both back to their defaults. */
static unsigned sanitize_pair(sh_settings_t *s, const sh_settings_t *defaults, sh_setting_t on, sh_setting_t off)
{
    if (s->v[off] < s->v[on]) {
        return 0;
    }
    s->v[on] = defaults->v[on];
    s->v[off] = defaults->v[off];
    return (1u << on) | (1u << off);
}

unsigned sh_settings_sanitize(sh_settings_t *s, const sh_settings_t *defaults)
{
    unsigned replaced = 0;
    for (int id = 0; id < SH_SET_COUNT; id++) {
        if (!sh_settings_value_ok((sh_setting_t)id, s->v[id])) {
            s->v[id] = defaults->v[id];
            replaced |= 1u << id;
        }
    }
    replaced |= sanitize_pair(s, defaults, SH_SET_DIFF_ON, SH_SET_DIFF_OFF);
    replaced |= sanitize_pair(s, defaults, SH_SET_DIFF_ON_W, SH_SET_DIFF_OFF_W);
    return replaced;
}

int16_t sh_settings_step(const sh_settings_t *s, sh_setting_t id, int16_t v, int dir)
{
    int16_t min, max;
    sh_settings_range(s, id, &min, &max);
    int next = v + (dir > 0 ? sh_setting_info[id].step : -sh_setting_info[id].step);
    if (next > max) {
        next = max;
    }
    if (next < min) {
        next = min;
    }
    return (int16_t)next;
}
