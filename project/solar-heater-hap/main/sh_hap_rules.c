/* Apple HomeKit: the rules — see sh_hap_rules.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarm.h"
#include "sh_settings.h"
#include "sh_hap_rules.h"

#define COL_FAULTS      (SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_E5))
#define TANK_FAULTS     (SH_ALARM_BIT(SH_ALARM_E2) | SH_ALARM_BIT(SH_ALARM_E4))

void sh_hap_view(const sh_hap_in_t *in, sh_hap_view_t *out)
{
    out->active = !in->vacation;
    if (!in->pump_on) {
        out->cur_state = SH_HAP_IDLE;
    } else {
        out->cur_state = in->reason == SH_PUMP_REASON_COOL ? SH_HAP_COOLING : SH_HAP_HEATING;
    }
    out->threshold = in->tmax;
    out->lock = in->lock;
    out->col_fault = (in->alarms & COL_FAULTS) != 0;
    out->tank_fault = (in->alarms & TANK_FAULTS) != 0;
}

unsigned sh_hap_changed(const sh_hap_view_t *was, const sh_hap_view_t *now)
{
    unsigned ch = 0;
    if (was->active != now->active) {
        ch |= SH_HAP_CH_ACTIVE;
    }
    if (was->cur_state != now->cur_state) {
        ch |= SH_HAP_CH_CUR_STATE;
    }
    if (was->threshold != now->threshold) {
        ch |= SH_HAP_CH_THRESHOLD;
    }
    if (was->lock != now->lock) {
        ch |= SH_HAP_CH_LOCK;
    }
    if (was->col_fault != now->col_fault) {
        ch |= SH_HAP_CH_COL_FAULT;
    }
    if (was->tank_fault != now->tank_fault) {
        ch |= SH_HAP_CH_TANK_FAULT;
    }
    return ch;
}

bool sh_hap_temp(int16_t t, int16_t min, int16_t max, int16_t *out)
{
    if (t == SH_TEMP_INVALID) {
        return false;
    }
    *out = t < min ? min : t > max ? max : t;
    return true;
}

bool sh_hap_write_active(int v, bool *vacation)
{
    if (v != 0 && v != 1) {
        return false;
    }
    *vacation = v == 0;
    return true;
}

bool sh_hap_write_target(int v)
{
    return v == SH_HAP_TARGET_HEAT;
}

bool sh_hap_write_threshold(float v, int16_t *tmax)
{
    const sh_setting_info_t *info = &sh_setting_info[SH_SET_TANK_MAX];
    /* the comparison is false for a NaN too */
    if (!(v >= (float)info->min - 0.5f && v <= (float)info->max + 0.5f)) {
        return false;
    }
    int16_t t = (int16_t)(v + 0.5f);        /* the range is positive */
    if (t < info->min) {
        t = info->min;
    } else if (t > info->max) {
        t = info->max;
    }
    *tmax = t;
    return true;
}

bool sh_hap_write_units(int v, bool *fahrenheit)
{
    if (v != SH_HAP_UNITS_C && v != SH_HAP_UNITS_F) {
        return false;
    }
    *fahrenheit = v == SH_HAP_UNITS_F;
    return true;
}

bool sh_hap_write_lock(int v, bool *lock)
{
    if (v != 0 && v != 1) {
        return false;
    }
    *lock = v == 1;
    return true;
}

int16_t sh_hap_display_temp(int16_t t, bool fahrenheit)
{
    if (!fahrenheit || t == SH_TEMP_INVALID) {
        return t;
    }
    const int32_t f = (int32_t)t * 9;       /* tenths of °F = tenths of °C * 9 / 5 + 320, rounded */
    return (int16_t)((f >= 0 ? f + 2 : f - 2) / 5 + 320);
}

bool sh_hap_start_due(bool enabled, bool connected, bool started)
{
    return enabled && connected && !started;
}

bool sh_hap_own_web(bool enabled, bool started, bool connected, bool ap)
{
    if (!enabled) {
        return connected || ap;
    }
    return !started && ap && !connected;
}

sh_hap_set_t sh_hap_set(bool enabled, bool want, bool paired)
{
    if (want == enabled) {
        return SH_HAP_SET_SAME;
    }
    if (!want && paired) {
        return SH_HAP_SET_REFUSED;
    }
    return SH_HAP_SET_RESTART;
}

bool sh_hap_window(bool open, sh_hap_evt_t evt, int paired)
{
    switch (evt) {
    case SH_HAP_EVT_STARTED:
    case SH_HAP_EVT_UNPAIRED:
        return paired == 0 ? true : evt == SH_HAP_EVT_STARTED ? false : open;
    case SH_HAP_EVT_PAIRED:
    case SH_HAP_EVT_TIMED_OUT:
        return false;
    }
    return open;
}
