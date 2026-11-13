/* Apple HomeKit rules — see sh_hap_rules.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarm.h"
#include "sh_hap_rules.h"

static int16_t clamp10(int16_t t, int min_c, int max_c)
{
    return t < min_c * 10 ? (int16_t)(min_c * 10) : t > max_c * 10 ? (int16_t)(max_c * 10) : t;
}

void sh_hap_view(const sh_hap_in_t *in, const sh_hap_view_t *prev, sh_hap_view_t *out)
{
    sh_hap_view_t v = { 0 };
    v.active = !in->vacation;
    v.state = !in->pump_on ? SH_HAP_STATE_IDLE : in->cooling ? SH_HAP_STATE_COOLING : SH_HAP_STATE_HEATING;
    v.tank = prev ? prev->tank : 0;
    v.col = prev ? prev->col : 0;
    if (!in->tank_fault && in->t_tank != SH_TEMP_INVALID) {
        v.tank = clamp10(in->t_tank, SH_HAP_TANK_MIN, SH_HAP_TANK_MAX);
    }
    if (!in->col_fault && in->t_col != SH_TEMP_INVALID) {
        v.col = clamp10(in->t_col, SH_HAP_COL_MIN, SH_HAP_COL_MAX);
    }
    v.threshold = (uint8_t)(in->tmax < SH_HAP_TMAX_MIN ? SH_HAP_TMAX_MIN
                            : in->tmax > SH_HAP_TMAX_MAX ? SH_HAP_TMAX_MAX : in->tmax);
    v.units = in->fahrenheit;
    v.lock = in->locked;
    v.col_fault = (in->alarms & (SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_E5))) != 0;
    v.tank_fault = (in->alarms & (SH_ALARM_BIT(SH_ALARM_E2) | SH_ALARM_BIT(SH_ALARM_E4))) != 0;
    *out = v;
}

unsigned sh_hap_notify(const sh_hap_view_t *prev, const sh_hap_view_t *now)
{
    unsigned n = 0;
    n |= prev->active != now->active ? SH_HAP_N_ACTIVE : 0u;
    n |= prev->state != now->state ? SH_HAP_N_STATE : 0u;
    n |= prev->threshold != now->threshold ? SH_HAP_N_THRESHOLD : 0u;
    n |= prev->units != now->units ? SH_HAP_N_UNITS : 0u;
    n |= prev->lock != now->lock ? SH_HAP_N_LOCK : 0u;
    n |= prev->col_fault != now->col_fault ? SH_HAP_N_COL_FAULT : 0u;
    n |= prev->tank_fault != now->tank_fault ? SH_HAP_N_TANK_FAULT : 0u;
    return n;
}

bool sh_hap_write_active(uint8_t v, bool *vacation)
{
    if (v > 1) {
        return false;
    }
    *vacation = v == 0;
    return true;
}

bool sh_hap_write_target(uint8_t v)
{
    return v == SH_HAP_TARGET_HEAT;
}

bool sh_hap_write_threshold(float c, int16_t *tmax)
{
    if (!(c >= SH_HAP_TMAX_MIN - 0.5f && c < SH_HAP_TMAX_MAX + 0.5f)) {    /* also NaN */
        return false;
    }
    *tmax = (int16_t)(c + 0.5f);
    return true;
}

bool sh_hap_write_flag(uint8_t v, bool *on)
{
    if (v > 1) {
        return false;
    }
    *on = v != 0;
    return true;
}

bool sh_hap_should_start(bool enabled, bool started, bool connected)
{
    return enabled && !started && connected;
}

bool sh_hap_own_web(bool enabled, bool started, bool connected, bool ap)
{
    if (!enabled) {
        return connected || ap;
    }
    return ap && !started;
}

sh_hap_sw_t sh_hap_switch(bool on, bool want, bool paired)
{
    if (on == want) {
        return SH_HAP_SW_NONE;
    }
    return !want && paired ? SH_HAP_SW_REFUSED : SH_HAP_SW_RESTART;
}

bool sh_hap_window(bool open, sh_hap_w_evt_t evt, int paired_count)
{
    switch (evt) {
    case SH_HAP_W_STARTED:
        return paired_count == 0;
    case SH_HAP_W_PAIRED:
    case SH_HAP_W_TIMEOUT:
        return false;
    case SH_HAP_W_UNPAIRED:
        return paired_count == 0 ? true : open;
    }
    return open;
}
