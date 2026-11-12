/* Pump decision — see sh_pump_logic.h. */

#include <stddef.h>
#include <string.h>
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

/* The temperature can be used: there is a value and the sensor is not declared faulty. */
static bool col_usable(const sh_pump_input_t *in)
{
    return !in->col_fault && in->t_col != SH_TEMP_INVALID;
}

static bool tank_usable(const sh_pump_input_t *in)
{
    return !in->tank_fault && in->t_tank != SH_TEMP_INVALID;
}

/* The two limits that stop or refuse a forced run. A limit whose sensor cannot be used cannot be
 * checked: its (old) latch does not count. */
static bool forced_blocked(const sh_pump_state_t *st, const sh_pump_input_t *in)
{
    return (col_usable(in) && st->col_boiling) || (tank_usable(in) && st->tank_ceiling);
}

/* What the decision functions need to know about the time. */
typedef struct {
    uint32_t now_s;
    uint32_t elapsed_s;         /* since the pump last changed */
    bool     off_time_over;     /* a forced run has just ended: the minimum off time is over */
} pump_time_t;

static bool min_off_pending(const sh_pump_params_t *p, const pump_time_t *t)
{
    return !t->off_time_over && t->elapsed_s < p->min_off_s;
}

/* ---- Normal mode: both temperatures valid ------------------------------------------------- */

static void decide_normal(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col,
                          int16_t t_tank, const pump_time_t *t)
{
    /* 1. Collector at the boiling point: steam, water hammer. A running pump stops too — if it ran,
     *    the flow did not cool the collector (no flow or steam in the circuit). */
    if (st->col_boiling) {
        st->col_protect = false;
        decided(st, false, SH_PUMP_REASON_COL_BOILING);
        return;
    }

    /* 2. Tank emergency ceiling: off at once, whatever the collector does. */
    if (st->tank_ceiling) {
        st->col_protect = false;
        decided(st, false, SH_PUMP_REASON_TANK_CEILING);
        return;
    }

    /* 3. Collector protection: on at once (no minimum off time, also above Tmax); ends below the
     *    threshold - 2 K once the pump has run for the minimum run time. It is set only here, where
     *    the pump is switched on, and cleared wherever the pump is stopped or taken over, so while
     *    it is set the pump is running and `elapsed_s` is its run time. */
    const int16_t protect = sh_pump_col_protect(p);
    if (t_col >= protect) {
        st->col_protect = true;
    } else if (st->col_protect && t_col < protect - SH_PUMP_RETURN_ZONE && t->elapsed_s >= p->min_run_s) {
        st->col_protect = false;
    }
    if (st->col_protect) {
        decided(st, true, SH_PUMP_REASON_COL_PROTECT);
        return;
    }

    /* 4. Tank Tmax: normal charging stops at once (a limit, not the difference). */
    if (st->tank_max) {
        decided(st, false, SH_PUMP_REASON_TANK_MAX);
        return;
    }

    /* 5. Difference, with the minimum times and the minimum collector temperature (it only blocks
     *    the start, a running pump is stopped by the difference). */
    const int32_t diff = (int32_t)t_col - t_tank;
    if (st->on) {
        if (diff > p->diff_off) {
            decided(st, true, SH_PUMP_REASON_DIFF);
        } else if (t->elapsed_s < p->min_run_s) {
            decided(st, true, SH_PUMP_REASON_MIN_RUN);
        } else {
            decided(st, false, SH_PUMP_REASON_DIFF);
        }
    } else {
        if (diff < p->diff_on) {
            decided(st, false, SH_PUMP_REASON_DIFF);
        } else if (t_col < p->col_min) {
            decided(st, false, SH_PUMP_REASON_COL_MIN);
        } else if (min_off_pending(p, t)) {
            decided(st, false, SH_PUMP_REASON_MIN_OFF);
        } else {
            decided(st, true, SH_PUMP_REASON_DIFF);
        }
    }
}

/* ---- Tank sensor faulty: the collector temperature alone ---------------------------------- */

/* With the pump running the collector outlet is warmer than the water at the bottom of the tank, so
 * the collector temperature is an upper bound for the tank: charging goes on up to 95 °C on the
 * collector. Tmax is not used. */
static void decide_tank_fault(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col,
                              const pump_time_t *t)
{
    if (st->col_boiling) {
        st->tf_probe = false;
        decided(st, false, SH_PUMP_REASON_COL_BOILING);
        return;
    }

    if (st->tf_probe) {
        /* Probe: the pump runs for the minimum run time, whatever the collector shows meanwhile. */
        if (t->now_s - st->tf_probe_s < p->min_run_s) {
            decided(st, true, SH_PUMP_REASON_TANK_FAULT_PROBE);
            return;
        }
        st->tf_probe = false;
        if (t_col >= SH_PUMP_TF_HOT - SH_PUMP_RETURN_ZONE) {
            /* the collector did not fall below 93 °C: the tank is full */
            st->tf_hot = true;
            st->tf_lock_s = t->now_s + SH_PUMP_PROBE_LOCK_S;
            decided(st, false, SH_PUMP_REASON_TANK_FAULT_HOT);
            return;
        }
        st->tf_hot = false;         /* the tank takes heat: charging goes on by the rules below */
    } else {
        /* Also with the pump off: a pump started above 95 °C would be stopped in the same second. */
        st->tf_hot = latch(st->tf_hot, t_col, SH_PUMP_TF_HOT);
    }

    if (st->tf_hot) {
        /* Collector at 95 °C: off at once. From there it can only go up to the collector protection
         * threshold, where one probe shows whether the tank still takes heat. */
        if (t_col >= sh_pump_col_protect(p) && t->now_s >= st->tf_lock_s) {
            st->tf_probe = true;
            st->tf_probe_s = t->now_s;
            decided(st, true, SH_PUMP_REASON_TANK_FAULT_PROBE);
            return;
        }
        decided(st, false, SH_PUMP_REASON_TANK_FAULT_HOT);
        return;
    }

    if (st->on) {
        if (t_col >= SH_PUMP_TF_START - SH_PUMP_RETURN_ZONE) {
            decided(st, true, SH_PUMP_REASON_TANK_FAULT);
        } else if (t->elapsed_s < p->min_run_s) {
            decided(st, true, SH_PUMP_REASON_MIN_RUN);
        } else {
            decided(st, false, SH_PUMP_REASON_TANK_FAULT);
        }
    } else {
        if (t_col < SH_PUMP_TF_START) {
            decided(st, false, SH_PUMP_REASON_TANK_FAULT);
        } else if (min_off_pending(p, t)) {
            decided(st, false, SH_PUMP_REASON_MIN_OFF);
        } else {
            decided(st, true, SH_PUMP_REASON_TANK_FAULT);
        }
    }
}

/* ---- Collector sensor faulty: probe runs, judged by the tank ------------------------------ */

/* The tank rose >= 0.5 K in the last 10 min with the pump off: only a heater can do that. */
static bool heater_running(const sh_pump_state_t *st, int16_t t_tank, uint32_t now_s)
{
    for (int i = st->hist_n - 1; i >= 0; i--) {
        if (now_s - st->hist[i].s >= SH_PUMP_CF_WINDOW_S) {
            return t_tank - st->hist[i].t >= SH_PUMP_CF_RISE;
        }
    }
    return false;                   /* off for less than 10 min: nothing to compare with */
}

static void cf_window_start(sh_pump_state_t *st, int16_t t_tank, uint32_t now_s)
{
    st->cf_window_s = now_s;
    st->cf_ref = t_tank;
}

static void decide_col_fault(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_tank,
                             const pump_time_t *t)
{
    if (!st->cf_active) {
        st->cf_active = true;
        st->cf_proved = false;
        if (st->on) {
            cf_window_start(st, t_tank, t->now_s);  /* a running pump goes on as the first probe */
        }
    }

    /* Tank at the ceiling: off at once. Whether the collector boils is not known, so no probe for
     * 5 h, without exceptions. */
    if (st->tank_ceiling) {
        if (!st->cf_ceiling) {
            st->cf_ceiling = true;
            st->cf_lock_s = t->now_s + SH_PUMP_PROBE_LOCK_S;
        }
        decided(st, false, SH_PUMP_REASON_TANK_CEILING);
        return;
    }
    st->cf_ceiling = false;

    if (st->on) {
        if (t->now_s - st->cf_window_s < SH_PUMP_CF_WINDOW_S) {
            decided(st, true, st->cf_proved ? SH_PUMP_REASON_COL_FAULT_CHARGE
                                            : SH_PUMP_REASON_COL_FAULT_PROBE);
            return;
        }
        if (t_tank - st->cf_ref >= SH_PUMP_CF_RISE) {
            /* the tank rose: there is sun; the next check in 10 min */
            st->cf_proved = true;
            st->cf_fails = 0;
            cf_window_start(st, t_tank, t->now_s);
            decided(st, true, SH_PUMP_REASON_COL_FAULT_CHARGE);
            return;
        }
        /* No rise. After a failed probe the wait doubles: 30 -> 60 -> 120 min. */
        uint32_t wait_s = SH_PUMP_CF_WAIT_S;
        if (!st->cf_proved) {
            for (uint8_t i = 0; i < st->cf_fails && wait_s < SH_PUMP_CF_WAIT_MAX_S; i++) {
                wait_s *= 2;
            }
            if (st->cf_fails < UINT8_MAX) {
                st->cf_fails++;
            }
        }
        st->cf_next_s = t->now_s + wait_s;
        decided(st, false, SH_PUMP_REASON_COL_FAULT_WAIT);
        return;
    }

    if (t->now_s < st->cf_lock_s) {
        decided(st, false, SH_PUMP_REASON_COL_FAULT_LOCK);
    } else if (t->now_s < st->cf_next_s) {
        decided(st, false, SH_PUMP_REASON_COL_FAULT_WAIT);
    } else if (min_off_pending(p, t)) {
        decided(st, false, SH_PUMP_REASON_MIN_OFF);
    } else if (heater_running(st, t_tank, t->now_s)) {
        /* its heat would be taken for heat from the collector: the probe waits */
        decided(st, false, SH_PUMP_REASON_HEATER);
    } else {
        st->cf_proved = false;
        cf_window_start(st, t_tank, t->now_s);
        decided(st, true, SH_PUMP_REASON_COL_FAULT_PROBE);
    }
}

/* ---- The decision by the state of the sensors --------------------------------------------- */

static void decide_by_sensors(const sh_pump_params_t *p, sh_pump_state_t *st,
                              const sh_pump_input_t *in, const pump_time_t *t)
{
    const bool col_ok = col_usable(in);
    const bool tank_ok = tank_usable(in);
    const bool tank_fault_mode = col_ok && in->tank_fault;
    const bool col_fault_mode = tank_ok && in->col_fault;

    /* A fault mode that is over leaves nothing behind, except the times before which no probe is
     * allowed: a sensor that fails again soon does not bring a probe earlier. */
    if (!tank_fault_mode) {
        st->tf_hot = false;
        st->tf_probe = false;
    }
    if (!col_fault_mode) {
        st->cf_active = false;
        st->cf_ceiling = false;
        st->cf_fails = 0;
    }

    if (col_ok && tank_ok) {
        /* also right after a fault: the pump stays as it is and the minimum times count from its
         * last switch, so a recovered sensor causes no extra switching */
        decide_normal(p, st, in->t_col, in->t_tank, t);
        return;
    }
    st->col_protect = false;
    if (tank_fault_mode) {
        decide_tank_fault(p, st, in->t_col, t);
    } else if (col_fault_mode) {
        decide_col_fault(p, st, in->t_tank, t);
    } else {
        /* both faulty, or a sensor that has not given a value yet: nothing can be judged */
        decided(st, false, SH_PUMP_REASON_SENSOR);
    }
}

/* ---- Forced runs: pump test, anti-seize --------------------------------------------------- */

/* The test stops the pump at its set end, but not before 1 min after the start. */
static uint32_t test_stop_s(const sh_pump_state_t *st)
{
    const uint32_t min_s = st->forced_start_s + SH_PUMP_TEST_MIN_S;
    return st->test_end_s > min_s ? st->test_end_s : min_s;
}

/* Set time left: what "+" and "-" work on. */
static uint32_t test_set_left(const sh_pump_state_t *st, uint32_t now_s)
{
    return st->test_end_s > now_s ? st->test_end_s - now_s : 0;
}

static void forced_start(sh_pump_state_t *st, sh_pump_forced_t forced, uint32_t now_s)
{
    st->forced = forced;
    st->forced_start_s = now_s;
    st->col_protect = false;        /* whatever ran the pump is taken over */
    st->tf_probe = false;
}

static bool forced_over(const sh_pump_state_t *st, uint32_t now_s)
{
    if (st->forced == SH_PUMP_FORCED_TEST) {
        return now_s >= test_stop_s(st);
    }
    return now_s - st->forced_start_s >= SH_PUMP_SEIZE_RUN_S;
}

/* 24 h without a run: turn the pump when that does not cool the tank, or after 12 h more. */
static bool anti_seize_due(const sh_pump_state_t *st, const sh_pump_input_t *in, uint32_t now_s)
{
    if (st->on) {
        return false;
    }
    const uint32_t idle_s = now_s - st->changed_s;
    if (idle_s < SH_PUMP_SEIZE_IDLE_S) {
        return false;
    }
    if (idle_s >= SH_PUMP_SEIZE_IDLE_S + SH_PUMP_SEIZE_WAIT_S) {
        return true;
    }
    return col_usable(in) && tank_usable(in) && in->t_col >= in->t_tank;
}

void sh_pump_test_plus(const sh_pump_params_t *p, sh_pump_state_t *st, const sh_pump_input_t *in,
                       uint32_t now_s)
{
    if (st->forced == SH_PUMP_FORCED_TEST) {
        uint32_t left_s = test_set_left(st, now_s) + SH_PUMP_TEST_STEP_S;
        if (left_s > SH_PUMP_TEST_MAX_S) {
            left_s = SH_PUMP_TEST_MAX_S;
        }
        st->test_end_s = now_s + left_s;
    } else if (!forced_blocked(st, in)) {
        forced_start(st, SH_PUMP_FORCED_TEST, now_s);   /* also takes over an anti-seize run */
        st->test_end_s = now_s + p->test_s;
    }
}

bool sh_pump_test_minus(sh_pump_state_t *st, uint32_t now_s)
{
    if (st->forced != SH_PUMP_FORCED_TEST) {
        return false;
    }
    if (test_set_left(st, now_s) > SH_PUMP_TEST_STEP_S) {
        st->test_end_s -= SH_PUMP_TEST_STEP_S;
    }
    return true;
}

bool sh_pump_test_stop(sh_pump_state_t *st, uint32_t now_s)
{
    if (st->forced != SH_PUMP_FORCED_TEST) {
        return false;
    }
    if (st->test_end_s > now_s) {
        st->test_end_s = now_s;
    }
    return true;
}

uint32_t sh_pump_test_left(const sh_pump_state_t *st, uint32_t now_s)
{
    if (st->forced != SH_PUMP_FORCED_TEST) {
        return 0;
    }
    const uint32_t stop_s = test_stop_s(st);
    return stop_s > now_s ? stop_s - now_s : 0;
}

/* ---- Tank temperature while the pump is off ----------------------------------------------- */

static void history_update(sh_pump_state_t *st, const sh_pump_input_t *in, uint32_t now_s)
{
    if (st->on || !tank_usable(in)) {
        st->hist_n = 0;
        return;
    }
    if (st->hist_n > 0 && now_s - st->hist[st->hist_n - 1].s < SH_PUMP_HIST_PERIOD_S) {
        return;
    }
    if (st->hist_n == SH_PUMP_HIST_COUNT) {
        memmove(&st->hist[0], &st->hist[1], (SH_PUMP_HIST_COUNT - 1) * sizeof(st->hist[0]));
        st->hist_n--;
    }
    st->hist[st->hist_n].s = now_s;
    st->hist[st->hist_n].t = in->t_tank;
    st->hist_n++;
}

/* ------------------------------------------------------------------------------------------- */

void sh_pump_decide(const sh_pump_params_t *p, sh_pump_state_t *st, const sh_pump_input_t *in,
                    uint32_t now_s)
{
    const bool was_on = st->on;
    bool restart = false;           /* a forced run has just ended: the times count from now */

    /* The limits follow every usable temperature, whatever the pump does. */
    if (col_usable(in)) {
        st->col_boiling = latch(st->col_boiling, in->t_col, SH_PUMP_COL_BOILING);
    }
    if (tank_usable(in)) {
        st->tank_ceiling = latch(st->tank_ceiling, in->t_tank, SH_PUMP_TANK_CEILING);
        st->tank_max = latch(st->tank_max, in->t_tank, p->tank_max);
    }

    if (st->forced == SH_PUMP_FORCED_NONE && !forced_blocked(st, in) && anti_seize_due(st, in, now_s)) {
        forced_start(st, SH_PUMP_FORCED_ANTI_SEIZE, now_s);
    }

    if (st->forced != SH_PUMP_FORCED_NONE) {
        if (!forced_blocked(st, in) && !forced_over(st, now_s)) {
            decided(st, true, st->forced == SH_PUMP_FORCED_TEST ? SH_PUMP_REASON_TEST
                                                                : SH_PUMP_REASON_ANTI_SEIZE);
        } else {
            /* The end: as from a stopped pump whose minimum off time is over. The pump stays on if
             * a rule starts it now; otherwise it stops and the off time counts from here. */
            st->forced = SH_PUMP_FORCED_NONE;
            st->on = false;
            restart = true;
        }
    }

    if (st->forced == SH_PUMP_FORCED_NONE) {
        const pump_time_t t = {
            .now_s = now_s,
            .elapsed_s = now_s - st->changed_s,
            .off_time_over = restart,
        };
        decide_by_sensors(p, st, in, &t);
    }

    if (st->on != was_on || restart) {
        st->changed_s = now_s;
    }
    history_update(st, in, now_s);
}

const char *sh_pump_reason_name(sh_pump_reason_t reason)
{
    static const char *const names[SH_PUMP_REASON_COUNT] = {
        [SH_PUMP_REASON_NONE]             = "none",
        [SH_PUMP_REASON_SENSOR]           = "sensor fault",
        [SH_PUMP_REASON_COL_BOILING]      = "collector at boiling point",
        [SH_PUMP_REASON_TANK_CEILING]     = "tank ceiling",
        [SH_PUMP_REASON_COL_PROTECT]      = "collector protection",
        [SH_PUMP_REASON_TANK_MAX]         = "tank Tmax",
        [SH_PUMP_REASON_DIFF]             = "difference",
        [SH_PUMP_REASON_MIN_RUN]          = "minimum run time",
        [SH_PUMP_REASON_MIN_OFF]          = "minimum off time",
        [SH_PUMP_REASON_COL_MIN]          = "collector below minimum",
        [SH_PUMP_REASON_TEST]             = "pump test",
        [SH_PUMP_REASON_ANTI_SEIZE]       = "anti-seize",
        [SH_PUMP_REASON_TANK_FAULT]       = "tank sensor fault: by the collector",
        [SH_PUMP_REASON_TANK_FAULT_HOT]   = "tank sensor fault: collector at 95",
        [SH_PUMP_REASON_TANK_FAULT_PROBE] = "tank sensor fault: probe",
        [SH_PUMP_REASON_COL_FAULT_PROBE]  = "collector sensor fault: probe",
        [SH_PUMP_REASON_COL_FAULT_CHARGE] = "collector sensor fault: tank rising",
        [SH_PUMP_REASON_COL_FAULT_WAIT]   = "collector sensor fault: waiting",
        [SH_PUMP_REASON_COL_FAULT_LOCK]   = "collector sensor fault: 5 h after tank ceiling",
        [SH_PUMP_REASON_HEATER]           = "collector sensor fault: heater running",
    };
    if ((unsigned)reason < SH_PUMP_REASON_COUNT && names[reason] != NULL) {
        return names[reason];
    }
    return "?";
}
