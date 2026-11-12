/* User settings: table, ranges and checks — see sh_settings.h. */

#include "sh_settings.h"

const sh_setting_info_t sh_setting_info[SH_SET_COUNT] = {
    [SH_SET_TANK_MAX] = { "tbo",  30,  85,  1 },
    [SH_SET_COL_MIN]  = { "c_lo", 10,  80,  1 },
    [SH_SET_DIFF_ON]  = { "d_on",  3,  20,  1 },
    [SH_SET_DIFF_OFF] = { "d_of",  1,  15,  1 },
    [SH_SET_TEST_S]   = { "t_st", 60, 900, 30 },
};

void sh_settings_range(const sh_settings_t *s, sh_setting_t id, int16_t *min, int16_t *max)
{
    *min = sh_setting_info[id].min;
    *max = sh_setting_info[id].max;
    if (id == SH_SET_DIFF_ON && s->v[SH_SET_DIFF_OFF] + 1 > *min) {
        *min = (int16_t)(s->v[SH_SET_DIFF_OFF] + 1);
    } else if (id == SH_SET_DIFF_OFF && s->v[SH_SET_DIFF_ON] - 1 < *max) {
        *max = (int16_t)(s->v[SH_SET_DIFF_ON] - 1);
    }
}

bool sh_settings_value_ok(sh_setting_t id, int16_t v)
{
    const sh_setting_info_t *i = &sh_setting_info[id];
    return v >= i->min && v <= i->max && (v - i->min) % i->step == 0;
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
    if (s->v[SH_SET_DIFF_OFF] >= s->v[SH_SET_DIFF_ON]) {
        s->v[SH_SET_DIFF_ON] = defaults->v[SH_SET_DIFF_ON];
        s->v[SH_SET_DIFF_OFF] = defaults->v[SH_SET_DIFF_OFF];
        replaced |= (1u << SH_SET_DIFF_ON) | (1u << SH_SET_DIFF_OFF);
    }
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
