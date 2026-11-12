/* Pump decision, normal mode — see sh_pump_logic.h. */

#include <stddef.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_pump_logic.h"

bool sh_pump_latch(bool active, int16_t t, int16_t limit)
{
    if (t >= limit) {
        return true;
    }
    if (t < limit - SH_PUMP_RETURN_ZONE) {
        return false;
    }
    return active;
}

static void decided(sh_pump_state_t *st, bool on, sh_pump_reason_t reason)
{
    st->on = on;
    st->reason = reason;
}

int16_t sh_pump_col_protect(const sh_pump_params_t *p)
{
    return p->col_protect < p->col_protect_cap ? p->col_protect : p->col_protect_cap;
}

void sh_pump_track_limits(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col, int16_t t_tank)
{
    if (t_col != SH_TEMP_INVALID) {
        st->col_boiling = sh_pump_latch(st->col_boiling, t_col, p->col_boiling);
    }
    if (t_tank != SH_TEMP_INVALID) {
        st->tank_ceiling = sh_pump_latch(st->tank_ceiling, t_tank, SH_PUMP_TANK_CEILING);
        st->tank_max = sh_pump_latch(st->tank_max, t_tank, p->tank_max);
    }
}

void sh_pump_decide(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col, int16_t t_tank,
                    uint32_t elapsed_s)
{
    const bool col_ok = t_col != SH_TEMP_INVALID;
    const bool tank_ok = t_tank != SH_TEMP_INVALID;

    /* The limits follow every valid temperature, whatever the pump does. */
    sh_pump_track_limits(p, st, t_col, t_tank);

    /* 1. Without both temperatures nothing can be judged: off (the sensor-fault modes are in
     *    sh_pump_ctl; the frost protection is not checked there). The protections end with the stop. */
    if (!col_ok || !tank_ok) {
        st->col_protect = false;
        st->frost = false;
        decided(st, false, SH_PUMP_REASON_SENSOR);
        return;
    }

    /* 2. Collector at the boiling point: steam, water hammer. A running pump stops too — if it ran,
     *    the flow did not cool the collector (no flow or steam in the circuit). */
    if (st->col_boiling) {
        st->col_protect = false;
        st->frost = false;
        decided(st, false, SH_PUMP_REASON_COL_BOILING);
        return;
    }

    /* 3. Tank emergency ceiling: off at once, whatever the collector does. */
    if (st->tank_ceiling) {
        st->col_protect = false;
        st->frost = false;
        decided(st, false, SH_PUMP_REASON_TANK_CEILING);
        return;
    }

    /* 4. Collector protection: on at once (no minimum off time, also above Tmax); ends below the
     *    threshold - 2 K once the pump has run for the minimum run time. It is set only here, where
     *    the pump is switched on, and cleared wherever the pump is stopped above, so while it is set
     *    the pump is running and `elapsed_s` is its run time. */
    const int16_t protect = sh_pump_col_protect(p);
    if (t_col >= protect) {
        st->col_protect = true;
    } else if (st->col_protect && t_col < protect - SH_PUMP_RETURN_ZONE && elapsed_s >= p->min_run_s) {
        st->col_protect = false;
    }
    if (st->col_protect) {
        st->frost = false;
        decided(st, true, SH_PUMP_REASON_COL_PROTECT);
        return;
    }

    /* 5. Frost protection: on at once (no minimum off time) when the collector is below the
     *    threshold, if the tank has heat to give (>= 5 °C); ends at collector >= threshold + 2 K once
     *    the pump has run for the minimum run time, and at once when the tank is below 5 °C. Like
     *    the collector protection it is set only where the pump is switched on. */
    const bool tank_cold = t_tank < SH_PUMP_FROST_TANK_MIN;
    if (p->frost_on && tank_cold && (st->frost || (!st->on && t_col < p->frost))) {
        st->frost = false;          /* no heat to give: off at once, whatever the minimum run time */
        decided(st, false, SH_PUMP_REASON_FROST_TANK);
        return;
    }
    if (!p->frost_on || tank_cold) {
        st->frost = false;
    } else if (t_col < p->frost) {
        st->frost = true;
    } else if (st->frost && t_col >= p->frost + SH_PUMP_RETURN_ZONE && elapsed_s >= p->min_run_s) {
        st->frost = false;
    }
    if (st->frost) {
        decided(st, true, SH_PUMP_REASON_FROST);
        return;
    }

    /* 6. Tank Tmax: normal charging stops at once (a limit, not the difference). */
    if (st->tank_max) {
        decided(st, false, SH_PUMP_REASON_TANK_MAX);
        return;
    }

    /* 7. Difference, with the minimum times and the minimum collector temperature (it only blocks
     *    the start, a running pump is stopped by the difference). */
    const int32_t diff = (int32_t)t_col - t_tank;
    if (st->on) {
        if (diff > p->diff_off) {
            decided(st, true, SH_PUMP_REASON_DIFF);
        } else if (elapsed_s < p->min_run_s) {
            decided(st, true, SH_PUMP_REASON_MIN_RUN);
        } else {
            decided(st, false, SH_PUMP_REASON_DIFF);
        }
    } else {
        if (diff < p->diff_on) {
            decided(st, false, SH_PUMP_REASON_DIFF);
        } else if (t_col < p->col_min) {
            decided(st, false, SH_PUMP_REASON_COL_MIN);
        } else if (elapsed_s < p->min_off_s) {
            decided(st, false, SH_PUMP_REASON_MIN_OFF);
        } else {
            decided(st, true, SH_PUMP_REASON_DIFF);
        }
    }
}

const char *sh_pump_reason_name(sh_pump_reason_t reason)
{
    static const char *const names[SH_PUMP_REASON_COUNT] = {
        [SH_PUMP_REASON_NONE]         = "none",
        [SH_PUMP_REASON_SENSOR]       = "sensor fault",
        [SH_PUMP_REASON_COL_BOILING]  = "collector at boiling point",
        [SH_PUMP_REASON_TANK_CEILING] = "tank ceiling",
        [SH_PUMP_REASON_COL_PROTECT]  = "collector protection",
        [SH_PUMP_REASON_FROST]        = "frost protection",
        [SH_PUMP_REASON_FROST_TANK]   = "frost protection: tank below 5 degC",
        [SH_PUMP_REASON_TANK_MAX]     = "tank Tmax",
        [SH_PUMP_REASON_DIFF]         = "difference",
        [SH_PUMP_REASON_MIN_RUN]      = "minimum run time",
        [SH_PUMP_REASON_MIN_OFF]      = "minimum off time",
        [SH_PUMP_REASON_COL_MIN]      = "collector below minimum",
        [SH_PUMP_REASON_TEST]         = "pump test",
        [SH_PUMP_REASON_ANTI_SEIZE]   = "anti-seize",
        [SH_PUMP_REASON_BOTH_FAULT]   = "both sensors faulty",
        [SH_PUMP_REASON_TF_RUN]       = "tank sensor faulty: collector warm",
        [SH_PUMP_REASON_TF_COLD]      = "tank sensor faulty: collector cold",
        [SH_PUMP_REASON_TF_HOT]       = "tank sensor faulty: collector hot",
        [SH_PUMP_REASON_TF_PROBE]     = "tank sensor faulty: probe at collector emergency",
        [SH_PUMP_REASON_CF_PROBE]     = "collector sensor faulty: probe",
        [SH_PUMP_REASON_CF_RUN]       = "collector sensor faulty: tank rises",
        [SH_PUMP_REASON_CF_WAIT]      = "collector sensor faulty: waiting for probe",
        [SH_PUMP_REASON_CF_HEATER]    = "collector sensor faulty: heater, probe postponed",
    };
    if ((unsigned)reason < SH_PUMP_REASON_COUNT && names[reason] != NULL) {
        return names[reason];
    }
    return "?";
}
