/* Pump decision for the normal (heating) mode — see sh_pump_logic.h.
 *
 * Order of the checks (doc/operating-modes.md 2.1):
 *   invalid sensor -> collector boiling -> tank emergency ceiling -> collector protection
 *   -> Tmax -> difference (with the minimum times and the minimum collector temperature).
 * The minimum run / off times apply only to the control by the difference; the protections
 * ignore them. Every single-value limit is active at >= value and released at < value - 2 K. */

#include <stddef.h>
#include "sh_pump_logic.h"
#include "sh_state.h"           /* SH_TEMP_INVALID */

/* Latch with a 2 K return zone: set at >= limit, cleared at < limit - 2 K. */
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

static void set(sh_pump_state_t *st, bool on, sh_pump_reason_t reason)
{
    st->on = on;
    st->reason = reason;
}

int16_t sh_pump_col_protect(const sh_pump_params_t *p)
{
    return p->col_protect < SH_PUMP_COL_PROTECT_CAP ? p->col_protect : SH_PUMP_COL_PROTECT_CAP;
}

void sh_pump_decide(const sh_pump_params_t *p, int16_t t_col, int16_t t_tank, uint32_t elapsed_s,
                    sh_pump_state_t *st)
{
    bool col_ok = t_col != SH_TEMP_INVALID;
    bool tank_ok = t_tank != SH_TEMP_INVALID;

    /* the latches follow the temperatures whatever the pump does */
    if (col_ok) {
        st->col_boiling = latch(st->col_boiling, t_col, SH_PUMP_COL_BOILING);
    }
    if (tank_ok) {
        st->tank_ceiling = latch(st->tank_ceiling, t_tank, SH_PUMP_TANK_CEILING);
        st->tank_max = latch(st->tank_max, t_tank, p->tank_max);
    }

    /* full sensor-failure logic is a later step: for now stop and stay off */
    if (!col_ok || !tank_ok) {
        set(st, false, SH_PUMP_REASON_SENSOR_INVALID);
        return;
    }
    /* steam in the collector: no start for any reason, a running pump stops too */
    if (st->col_boiling) {
        set(st, false, SH_PUMP_REASON_COL_BOILING);
        return;
    }
    if (st->tank_ceiling) {
        set(st, false, SH_PUMP_REASON_TANK_CEILING);
        return;
    }

    /* collector protection: on at once (ignores the minimum off time and Tmax); off under the
     * threshold - 2 K, but only after the minimum run time */
    int16_t protect = sh_pump_col_protect(p);
    if (t_col >= protect) {
        set(st, true, SH_PUMP_REASON_COL_PROTECT);
        return;
    }
    if (st->on && st->reason == SH_PUMP_REASON_COL_PROTECT &&
        (t_col >= protect - SH_PUMP_RETURN_ZONE || elapsed_s < p->min_run_s)) {
        return;                                     /* stays on, same reason */
    }

    /* Tmax stops the normal charging at once (it is a limit, not the difference) */
    if (st->tank_max) {
        set(st, false, SH_PUMP_REASON_TANK_MAX);
        return;
    }

    /* control by the difference */
    int32_t diff = (int32_t)t_col - t_tank;
    if (st->on) {
        if (diff > p->diff_off) {
            set(st, true, SH_PUMP_REASON_DIFF_ON);
        } else if (elapsed_s < p->min_run_s) {
            set(st, true, SH_PUMP_REASON_MIN_RUN);
        } else {
            set(st, false, SH_PUMP_REASON_DIFF_LOW);
        }
    } else {
        if (diff < p->diff_on) {
            set(st, false, SH_PUMP_REASON_DIFF_LOW);
        } else if (t_col < p->col_min) {
            set(st, false, SH_PUMP_REASON_COL_COLD);
        } else if (elapsed_s < p->min_off_s) {
            set(st, false, SH_PUMP_REASON_MIN_OFF);
        } else {
            set(st, true, SH_PUMP_REASON_DIFF_ON);
        }
    }
}

const char *sh_pump_reason_str(sh_pump_reason_t reason)
{
    static const char *const names[] = {
        [SH_PUMP_REASON_START]          = "start",
        [SH_PUMP_REASON_SENSOR_INVALID] = "sensor invalid",
        [SH_PUMP_REASON_COL_BOILING]    = "collector boiling",
        [SH_PUMP_REASON_TANK_CEILING]   = "tank ceiling",
        [SH_PUMP_REASON_COL_PROTECT]    = "collector protection",
        [SH_PUMP_REASON_TANK_MAX]       = "tank Tmax",
        [SH_PUMP_REASON_DIFF_ON]        = "difference",
        [SH_PUMP_REASON_MIN_RUN]        = "min run time",
        [SH_PUMP_REASON_DIFF_LOW]       = "difference low",
        [SH_PUMP_REASON_COL_COLD]       = "collector under min",
        [SH_PUMP_REASON_MIN_OFF]        = "min off time",
    };
    if ((unsigned)reason < sizeof(names) / sizeof(names[0]) && names[reason] != NULL) {
        return names[reason];
    }
    return "?";
}
