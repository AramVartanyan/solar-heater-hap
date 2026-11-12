/* User settings: the list, the ranges and the checks — see sh_settings_logic.h. */

#include "sh_settings_logic.h"

static const sh_setting_range_t s_limits[SH_SET_COUNT] = {
    [SH_SET_TANK_MAX] = { 30, 85, 1 },
    [SH_SET_COL_MIN]  = { 10, 80, 1 },
    [SH_SET_DIFF_ON]  = { 3, 20, 1 },
    [SH_SET_DIFF_OFF] = { 1, 15, 1 },
    [SH_SET_TEST_S]   = { 60, 900, 30 },
};

sh_setting_range_t sh_setting_limits(sh_setting_id_t id)
{
    return s_limits[id];
}

sh_setting_range_t sh_setting_range(const sh_settings_t *s, sh_setting_id_t id)
{
    sh_setting_range_t r = s_limits[id];
    if (id == SH_SET_DIFF_ON && r.min < s->v[SH_SET_DIFF_OFF] + 1) {
        r.min = s->v[SH_SET_DIFF_OFF] + 1;
    }
    if (id == SH_SET_DIFF_OFF && r.max > s->v[SH_SET_DIFF_ON] - 1) {
        r.max = s->v[SH_SET_DIFF_ON] - 1;
    }
    return r;
}

void sh_settings_check(sh_settings_t *out, const sh_settings_t *stored, uint32_t present,
                       const sh_settings_t *defaults)
{
    for (int id = 0; id < SH_SET_COUNT; id++) {
        const int16_t v = stored->v[id];
        const bool good = (present & (1u << id)) && v >= s_limits[id].min && v <= s_limits[id].max;
        out->v[id] = good ? v : defaults->v[id];
    }
    if (out->v[SH_SET_DIFF_OFF] >= out->v[SH_SET_DIFF_ON]) {
        out->v[SH_SET_DIFF_ON] = defaults->v[SH_SET_DIFF_ON];
        out->v[SH_SET_DIFF_OFF] = defaults->v[SH_SET_DIFF_OFF];
    }
}
