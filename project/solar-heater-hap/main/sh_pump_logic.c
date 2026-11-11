/* Pump decision, normal mode — see sh_pump_logic.h. */

#include <stddef.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_pump_logic.h"

/* A single-value limit with the 2 K return zone: set at >= limit, cleared below limit - 2 K. */
static bool latch(bool active, int16_t t, int16_t limit)
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
    return p->col_protect < SH_PUMP_COL_PROTECT_CAP ? p->col_protect : SH_PUMP_COL_PROTECT_CAP;
}

void sh_pump_decide(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col, int16_t t_tank,
                    uint32_t elapsed_s)
{
    const bool col_ok = t_col != SH_TEMP_INVALID;
    const bool tank_ok = t_tank != SH_TEMP_INVALID;

    /* The limits follow every valid temperature, whatever the pump does. */
    if (col_ok) {
        st->col_boiling = latch(st->col_boiling, t_col, SH_PUMP_COL_BOILING);
    }
    if (tank_ok) {
        st->tank_ceiling = latch(st->tank_ceiling, t_tank, SH_PUMP_TANK_CEILING);
        st->tank_max = latch(st->tank_max, t_tank, p->tank_max);
    }

    /* 1. Without both temperatures nothing can be judged: off (the full sensor-fault logic is a
     *    later step). The collector protection ends with the stop. */
    if (!col_ok || !tank_ok) {
        st->col_protect = false;
        decided(st, false, SH_PUMP_REASON_SENSOR);
        return;
    }

    /* 2. Collector at the boiling point: steam, water hammer. A running pump stops too — if it ran,
     *    the flow did not cool the collector (no flow or steam in the circuit). */
    if (st->col_boiling) {
        st->col_protect = false;
        decided(st, false, SH_PUMP_REASON_COL_BOILING);
        return;
    }

    /* 3. Tank emergency ceiling: off at once, whatever the collector does. */
    if (st->tank_ceiling) {
        st->col_protect = false;
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
        decided(st, true, SH_PUMP_REASON_COL_PROTECT);
        return;
    }

    /* 5. Tank Tmax: normal charging stops at once (a limit, not the difference). */
    if (st->tank_max) {
        decided(st, false, SH_PUMP_REASON_TANK_MAX);
        return;
    }

    /* 6. Difference, with the minimum times and the minimum collector temperature (it only blocks
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
        [SH_PUMP_REASON_TANK_MAX]     = "tank Tmax",
        [SH_PUMP_REASON_DIFF]         = "difference",
        [SH_PUMP_REASON_MIN_RUN]      = "minimum run time",
        [SH_PUMP_REASON_MIN_OFF]      = "minimum off time",
        [SH_PUMP_REASON_COL_MIN]      = "collector below minimum",
    };
    if ((unsigned)reason < SH_PUMP_REASON_COUNT && names[reason] != NULL) {
        return names[reason];
    }
    return "?";
}
