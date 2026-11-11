#include "sh_state.h"       /* SH_TEMP_INVALID */
#include "sh_pump.h"

static bool decided(sh_pump_state_t *st, bool on, sh_pump_reason_t reason)
{
    st->on = on;
    st->reason = reason;
    return on;
}

bool sh_pump_decide(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col,
                    int16_t t_tank, uint32_t elapsed_s)
{
    /* 0. Without both temperatures nothing can be judged: off, and the limits keep their memory. */
    if (t_col == SH_TEMP_INVALID || t_tank == SH_TEMP_INVALID) {
        st->col_protect = false;
        return decided(st, false, SH_PUMP_REASON_SENSOR);
    }

    /* Limits with the 2 K return zone: set at the limit, cleared below the zone. */
    if (t_col >= SH_PUMP_COL_BOIL) {
        st->col_boil = true;
    } else if (t_col < SH_PUMP_COL_BOIL - SH_PUMP_HYST) {
        st->col_boil = false;
    }
    if (t_tank >= SH_PUMP_TANK_CEILING) {
        st->tank_ceiling = true;
    } else if (t_tank < SH_PUMP_TANK_CEILING - SH_PUMP_HYST) {
        st->tank_ceiling = false;
    }
    if (t_tank >= p->tank_max) {
        st->tank_max = true;
    } else if (t_tank <= p->tank_max - SH_PUMP_HYST) {
        st->tank_max = false;
    }

    /* 1. Collector at the boiling point: a stopped pump stays stopped (steam, water hammer).
     *    A running pump is not stopped by this rule. */
    if (!st->on && st->col_boil) {
        return decided(st, false, SH_PUMP_REASON_COL_BOIL);
    }

    /* 2. Tank emergency ceiling: off at once, whatever the collector does. */
    if (st->tank_ceiling) {
        st->col_protect = false;
        return decided(st, false, SH_PUMP_REASON_TANK_CEILING);
    }

    /* 3. Collector protection: on at once (no minimum off time, also above Tmax); ends below
     *    the threshold - 2 K, once the pump has run for the minimum run time. */
    int col_protect = p->col_protect < SH_PUMP_COL_PROTECT_CAP ? p->col_protect
                                                               : SH_PUMP_COL_PROTECT_CAP;
    if (t_col >= col_protect) {
        st->col_protect = true;
    } else if (st->col_protect && t_col < col_protect - SH_PUMP_HYST && elapsed_s >= p->min_run_s) {
        st->col_protect = false;
    }
    if (st->col_protect) {
        return decided(st, true, SH_PUMP_REASON_COL_PROTECT);
    }

    /* 4. Tank Tmax: normal charging stops at once, allowed again at Tmax - 2 K. */
    if (st->tank_max) {
        return decided(st, false, SH_PUMP_REASON_TANK_MAX);
    }

    /* 5. Difference, with the minimum times and the minimum collector temperature. */
    int diff = t_col - t_tank;
    if (st->on) {
        if (diff > p->dt_off) {
            return decided(st, true, SH_PUMP_REASON_DIFF);
        }
        if (elapsed_s < p->min_run_s) {
            return decided(st, true, SH_PUMP_REASON_MIN_RUN);
        }
        return decided(st, false, SH_PUMP_REASON_DIFF);
    }
    if (diff < p->dt_on) {
        return decided(st, false, SH_PUMP_REASON_DIFF);
    }
    if (t_col < p->col_min) {
        return decided(st, false, SH_PUMP_REASON_COL_MIN);
    }
    if (elapsed_s < p->min_off_s) {
        return decided(st, false, SH_PUMP_REASON_MIN_OFF);
    }
    return decided(st, true, SH_PUMP_REASON_DIFF);
}

const char *sh_pump_reason_name(sh_pump_reason_t reason)
{
    switch (reason) {
    case SH_PUMP_REASON_SENSOR:         return "sensor fault";
    case SH_PUMP_REASON_COL_BOIL:       return "collector at boiling point";
    case SH_PUMP_REASON_TANK_CEILING:   return "tank ceiling";
    case SH_PUMP_REASON_COL_PROTECT:    return "collector protection";
    case SH_PUMP_REASON_TANK_MAX:       return "tank Tmax";
    case SH_PUMP_REASON_DIFF:           return "difference";
    case SH_PUMP_REASON_MIN_RUN:        return "minimum run time";
    case SH_PUMP_REASON_MIN_OFF:        return "minimum off time";
    case SH_PUMP_REASON_COL_MIN:        return "collector below minimum";
    case SH_PUMP_REASON_NONE:
    default:                            return "none";
    }
}
