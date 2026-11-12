/* Pump control above the normal-mode decision — see sh_pump_ctl.h. */

#include <stddef.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_pump_ctl.h"

#define LONG_AGO_S      0x7FFFFFFFu     /* "the minimum off time is over" */

static void set(sh_pump_ctl_t *c, bool on, sh_pump_reason_t reason, uint32_t now_s)
{
    if (on != c->on) {
        c->switched_s = now_s;
    }
    c->on = on;
    c->reason = reason;
}

/* The only limits that stop the test, the air venting and the anti-seize run. A limit whose sensor
 * is faulty (passed as SH_TEMP_INVALID) cannot be checked; the tank ceiling with a faulty collector
 * stops them (whether the collector is hot is not known). */
static bool emergency(const sh_pump_ctl_t *c, int16_t col, int16_t tank, sh_pump_reason_t *reason)
{
    if (col != SH_TEMP_INVALID && c->norm.col_boiling) {
        *reason = SH_PUMP_REASON_COL_BOILING;
        return true;
    }
    if (tank != SH_TEMP_INVALID && c->norm.tank_ceiling && (col == SH_TEMP_INVALID || c->norm.col_hot)) {
        *reason = SH_PUMP_REASON_TANK_CEILING;
        return true;
    }
    return false;
}

/* One tank sample a minute while the pump is off; a running pump or no tank value restarts it. */
static void track_history(sh_pump_ctl_t *c, int16_t tank, uint32_t now_s)
{
    if (c->on || tank == SH_TEMP_INVALID) {
        c->hist_n = 0;
        c->hist_next_s = now_s;
        return;
    }
    if (now_s < c->hist_next_s) {
        return;
    }
    c->hist[c->hist_head] = tank;
    c->hist_head = (uint8_t)((c->hist_head + 1) % SH_CF_HISTORY);
    if (c->hist_n < SH_CF_HISTORY) {
        c->hist_n++;
    }
    c->hist_next_s = now_s + 60;
}

bool sh_pump_ctl_heater(const sh_pump_ctl_t *c)
{
    if (c->hist_n < SH_CF_HISTORY) {
        return false;               /* the pump has not been off for 10 min yet */
    }
    /* full ring: hist_head is the oldest sample (10 min ago), the one before it the newest */
    const int16_t oldest = c->hist[c->hist_head];
    const int16_t newest = c->hist[(c->hist_head + SH_CF_HISTORY - 1) % SH_CF_HISTORY];
    return newest - oldest >= SH_CF_RISE;
}

static sh_pump_mode_t mode_of(const sh_pump_temps_t *t)
{
    if (t->col_fault && t->tank_fault) {
        return SH_PUMP_MODE_BOTH_FAULT;
    }
    if (t->tank_fault) {
        return SH_PUMP_MODE_TANK_FAULT;
    }
    if (t->col_fault) {
        return SH_PUMP_MODE_COL_FAULT;
    }
    return SH_PUMP_MODE_NORMAL;
}

/* A new mode starts with the current pump state and the time of its last switch; only its own
 * memory starts over. */
static void enter_mode(sh_pump_ctl_t *c, sh_pump_mode_t mode, uint32_t now_s)
{
    c->mode = mode;
    c->tf_probe = false;
    c->tf_next_probe_s = now_s;     /* an emergency probe is allowed at once */
    c->cf_probe = false;
    c->cf_fails = 0;
    c->cf_ceiling = false;
    c->cf_next_s = now_s;           /* the first probe at once (the minimum off time applies) */
    c->norm.col_protect = false;
    c->norm.frost = false;
    c->norm.cool = false;
}

static bool normal_mode(sh_pump_ctl_t *c, const sh_pump_params_t *p, int16_t col, int16_t tank,
                        bool was_on, uint32_t elapsed_s, sh_pump_reason_t *reason)
{
    c->norm.on = was_on;
    if (!was_on) {
        c->norm.col_protect = false;    /* set only while the pump runs by them */
        c->norm.frost = false;
        c->norm.cool = false;
    }
    sh_pump_decide(p, &c->norm, col, tank, elapsed_s);
    *reason = c->norm.reason;
    return c->norm.on;
}

/* Tank sensor faulty: the collector is the only measure (doc 2.1, p. 12). */
static bool tank_fault_mode(sh_pump_ctl_t *c, const sh_pump_params_t *p, int16_t col, bool was_on,
                            uint32_t elapsed_s, uint32_t now_s, sh_pump_reason_t *reason)
{
    if (col == SH_TEMP_INVALID) {           /* no collector value since boot yet */
        c->tf_probe = false;
        *reason = SH_PUMP_REASON_SENSOR;
        return false;
    }
    if (c->norm.col_boiling) {
        if (c->tf_probe) {                  /* the probe failed */
            c->tf_probe = false;
            c->tf_next_probe_s = now_s + SH_TF_PROBE_RETRY_S;
        }
        *reason = SH_PUMP_REASON_COL_BOILING;
        return false;
    }

    /* Emergency probe: the collector at its protection threshold with the pump stopped -> run for
     * the minimum run time (no minimum off time). A running pump that 95 °C would stop becomes
     * the probe, without switching off and on. */
    if (!c->tf_probe && col >= sh_pump_col_protect(p) && now_s >= c->tf_next_probe_s &&
        (!was_on || c->tf_hot)) {
        c->tf_probe = true;
        c->tf_probe_s = now_s;
    }
    if (c->tf_probe) {
        if (now_s - c->tf_probe_s < p->min_run_s) {
            *reason = SH_PUMP_REASON_TF_PROBE;
            return true;
        }
        c->tf_probe = false;
        if (col >= SH_TF_HOT - SH_PUMP_RETURN_ZONE) {
            /* still >= 93 °C: the tank does not take the heat (full) */
            c->tf_next_probe_s = now_s + SH_TF_PROBE_RETRY_S;
            *reason = SH_PUMP_REASON_TF_HOT;
            return false;
        }
        /* below 93 °C: the tank takes the heat, charging goes on by the rules below */
    }

    if (c->tf_hot) {
        *reason = SH_PUMP_REASON_TF_HOT;
        return false;
    }
    if (was_on) {
        if (col >= SH_TF_STOP) {
            *reason = SH_PUMP_REASON_TF_RUN;
            return true;
        }
        if (elapsed_s < p->min_run_s) {
            *reason = SH_PUMP_REASON_MIN_RUN;
            return true;
        }
        *reason = SH_PUMP_REASON_TF_COLD;
        return false;
    }
    if (col < SH_TF_START) {
        *reason = SH_PUMP_REASON_TF_COLD;
        return false;
    }
    if (elapsed_s < p->min_off_s) {
        *reason = SH_PUMP_REASON_MIN_OFF;
        return false;
    }
    *reason = SH_PUMP_REASON_TF_RUN;
    return true;
}

static void cf_start_probe(sh_pump_ctl_t *c, int16_t tank, uint32_t now_s)
{
    c->cf_probe = true;
    c->cf_rise_seen = false;
    c->cf_ref = tank;
    c->cf_check_s = now_s;
}

/* Wait before the next probe: 30 min, doubled after every failed probe in a row, at most 2 h. */
static uint32_t cf_wait(uint8_t fails)
{
    uint32_t wait = SH_CF_WAIT_S;
    for (uint8_t i = 1; i < fails && wait < SH_CF_WAIT_MAX_S; i++) {
        wait *= 2;
    }
    return wait < SH_CF_WAIT_MAX_S ? wait : SH_CF_WAIT_MAX_S;
}

/* Collector sensor faulty: probes, judged by the tank (doc 2.1, p. 12). */
static bool col_fault_mode(sh_pump_ctl_t *c, const sh_pump_params_t *p, int16_t tank, bool was_on,
                           uint32_t elapsed_s, uint32_t now_s, sh_pump_reason_t *reason)
{
    if (tank == SH_TEMP_INVALID) {          /* no tank value since boot yet */
        c->cf_probe = false;
        *reason = SH_PUMP_REASON_SENSOR;
        return false;
    }
    if (c->norm.tank_ceiling) {
        if (!c->cf_ceiling) {
            /* tank at 95 °C (by the pump or by the heater): whether the collector boils is not known,
             * no probe for 5 h, no exception */
            c->cf_ceiling = true;
            c->cf_next_s = now_s + SH_CF_CEILING_WAIT_S;
        }
        c->cf_probe = false;
        *reason = SH_PUMP_REASON_TANK_CEILING;
        return false;
    }
    c->cf_ceiling = false;
    if (was_on && !c->cf_probe) {
        cf_start_probe(c, tank, now_s);     /* the fault found the pump running: a probe from now */
    }
    if (c->cf_probe) {
        if (now_s - c->cf_check_s < SH_CF_WINDOW_S) {
            *reason = c->cf_rise_seen ? SH_PUMP_REASON_CF_RUN : SH_PUMP_REASON_CF_PROBE;
            return true;
        }
        if (tank - c->cf_ref >= SH_CF_RISE) {   /* sun: go on, check again in 10 min */
            c->cf_rise_seen = true;
            c->cf_fails = 0;
            c->cf_ref = tank;
            c->cf_check_s = now_s;
            *reason = SH_PUMP_REASON_CF_RUN;
            return true;
        }
        c->cf_probe = false;
        if (!c->cf_rise_seen && c->cf_fails < 3) {
            c->cf_fails++;
        }
        c->cf_next_s = now_s + cf_wait(c->cf_fails);
        *reason = SH_PUMP_REASON_CF_WAIT;
        return false;
    }
    if (now_s < c->cf_next_s) {
        *reason = SH_PUMP_REASON_CF_WAIT;
        return false;
    }
    if (sh_pump_ctl_heater(c)) {
        *reason = SH_PUMP_REASON_CF_HEATER;
        return false;
    }
    if (elapsed_s < p->min_off_s) {
        *reason = SH_PUMP_REASON_MIN_OFF;
        return false;
    }
    cf_start_probe(c, tank, now_s);
    *reason = SH_PUMP_REASON_CF_PROBE;
    return true;
}

static bool seize_due(const sh_pump_ctl_t *c, int16_t col, int16_t tank, uint32_t now_s)
{
    if (c->on || c->mode == SH_PUMP_MODE_BOTH_FAULT) {
        return false;
    }
    const uint32_t idle_s = now_s - c->switched_s;
    sh_pump_reason_t unused;
    if (idle_s < SH_SEIZE_IDLE_S || emergency(c, col, tank, &unused)) {
        return false;
    }
    if (idle_s >= SH_SEIZE_FORCE_S) {
        return true;
    }
    return col != SH_TEMP_INVALID && tank != SH_TEMP_INVALID && col >= tank;
}

void sh_pump_ctl_step(sh_pump_ctl_t *c, const sh_pump_params_t *p, const sh_pump_temps_t *t,
                      uint32_t now_s)
{
    /* A faulty sensor's last valid value is not used for any decision. */
    const int16_t col = t->col_fault ? SH_TEMP_INVALID : t->t_col;
    const int16_t tank = t->tank_fault ? SH_TEMP_INVALID : t->t_tank;

    sh_pump_track_limits(p, &c->norm, col, tank);
    if (col != SH_TEMP_INVALID) {
        c->tf_hot = sh_pump_latch(c->tf_hot, col, SH_TF_HOT);
    }
    track_history(c, tank, now_s);

    const sh_pump_mode_t mode = mode_of(t);
    if (mode != c->mode) {
        enter_mode(c, mode, now_s);
    }

    sh_pump_reason_t reason;
    bool restart = false;               /* the test or the anti-seize run just ended */

    /* The test, the air venting and the anti-seize run end when their time is over or when the boiling point or the
     * tank ceiling stops them; they do not start again by themselves. A test asked for while such a
     * limit is active ends in the same step (refused). */
    if (c->test) {
        if (now_s < c->test_end_s && !emergency(c, col, tank, &reason)) {
            set(c, true, SH_PUMP_REASON_TEST, now_s);
            return;
        }
        c->test = false;
        c->events |= SH_PUMP_EVT_TEST_END;
        restart = true;
    }
    if (c->air) {
        const uint32_t t = now_s - c->air_start_s;
        if (t < sh_air_total_s(c->air_pause_s) && !emergency(c, col, tank, &reason)) {
            const bool run = t % (SH_AIR_RUN_S + c->air_pause_s) < SH_AIR_RUN_S;
            set(c, run, run ? SH_PUMP_REASON_AIR_RUN : SH_PUMP_REASON_AIR_PAUSE, now_s);
            return;
        }
        c->air = false;
        c->events |= SH_PUMP_EVT_AIR_END;
        restart = true;
    }
    if (c->seize) {
        if (now_s - c->seize_start_s < SH_SEIZE_RUN_S && !emergency(c, col, tank, &reason)) {
            set(c, true, SH_PUMP_REASON_ANTI_SEIZE, now_s);
            return;
        }
        c->seize = false;
        c->events |= SH_PUMP_EVT_SEIZE_END;
        restart = true;
    }

    /* After the test / anti-seize run: like a stopped pump whose minimum off time is over. */
    const bool was_on = restart ? false : c->on;
    const uint32_t elapsed_s = restart ? LONG_AGO_S : now_s - c->switched_s;
    bool on;
    switch (c->mode) {
    case SH_PUMP_MODE_NORMAL:
        on = normal_mode(c, p, col, tank, was_on, elapsed_s, &reason);
        break;
    case SH_PUMP_MODE_TANK_FAULT:
        on = tank_fault_mode(c, p, col, was_on, elapsed_s, now_s, &reason);
        break;
    case SH_PUMP_MODE_COL_FAULT:
        on = col_fault_mode(c, p, tank, was_on, elapsed_s, now_s, &reason);
        break;
    default:
        on = false;
        reason = SH_PUMP_REASON_BOTH_FAULT;
        break;
    }

    if (!on && seize_due(c, col, tank, now_s)) {
        c->seize = true;
        c->seize_start_s = now_s;
        c->events |= SH_PUMP_EVT_SEIZE_START;
        on = true;
        reason = SH_PUMP_REASON_ANTI_SEIZE;
    }
    set(c, on, reason, now_s);
    if (restart && on) {
        c->switched_s = now_s;          /* a pump that goes on runs from now: its minimum run time too */
    }
}

/* The test never ends earlier than 1 min after its start. */
static void test_min_length(sh_pump_ctl_t *c)
{
    if (c->test_end_s < c->test_start_s + SH_PUMP_TEST_MIN_S) {
        c->test_end_s = c->test_start_s + SH_PUMP_TEST_MIN_S;
    }
}

uint32_t sh_pump_ctl_test_left(const sh_pump_ctl_t *c, uint32_t now_s)
{
    return c->test && c->test_end_s > now_s ? c->test_end_s - now_s : 0;
}

uint32_t sh_air_total_s(uint32_t pause_s)
{
    return SH_AIR_CYCLES * (SH_AIR_RUN_S + pause_s);
}

void sh_pump_ctl_air_start(sh_pump_ctl_t *c, uint32_t pause_s, uint32_t now_s)
{
    if (c->air || c->test) {
        return;
    }
    /* at once, no minimum off time; replaces an anti-seize run or a probe (like the test) */
    c->air = true;
    c->air_start_s = now_s;
    c->air_pause_s = pause_s;
    c->seize = false;
    c->tf_probe = false;
    c->cf_probe = false;
    c->events |= SH_PUMP_EVT_AIR_START;
}

void sh_pump_ctl_air_stop(sh_pump_ctl_t *c)
{
    if (c->air) {
        c->air_pause_s = 0;
        c->air_start_s -= sh_air_total_s(0);    /* its time is over: the next step ends it */
    }
}

void sh_pump_ctl_air(const sh_pump_ctl_t *c, uint32_t now_s, sh_air_view_t *out)
{
    const uint32_t total = sh_air_total_s(c->air_pause_s);
    const uint32_t t = now_s - c->air_start_s;
    out->on = c->air && t < total;
    out->run = false;
    out->cycle = 0;
    out->left_s = 0;
    if (out->on) {
        const uint32_t period = SH_AIR_RUN_S + c->air_pause_s;
        out->run = t % period < SH_AIR_RUN_S;
        out->cycle = (uint8_t)(t / period + 1);
        out->left_s = total - t;
    }
}

void sh_pump_ctl_plus(sh_pump_ctl_t *c, uint32_t test_s, uint32_t now_s)
{
    if (c->air) {
        return;                         /* no test during the air venting */
    }
    if (!c->test) {
        /* start: at once, no minimum off time; replaces an anti-seize run or a probe */
        c->test = true;
        c->test_start_s = now_s;
        c->test_end_s = now_s + test_s;
        test_min_length(c);
        c->seize = false;
        c->tf_probe = false;
        c->cf_probe = false;
        c->events |= SH_PUMP_EVT_TEST_START;
        return;
    }
    uint32_t left = sh_pump_ctl_test_left(c, now_s) + SH_PUMP_TEST_STEP_S;
    if (left > SH_PUMP_TEST_MAX_S) {
        left = SH_PUMP_TEST_MAX_S;
    }
    c->test_end_s = now_s + left;
}

void sh_pump_ctl_minus(sh_pump_ctl_t *c, uint32_t now_s)
{
    if (c->test && sh_pump_ctl_test_left(c, now_s) > SH_PUMP_TEST_STEP_S) {
        c->test_end_s -= SH_PUMP_TEST_STEP_S;
        test_min_length(c);
    }
}

void sh_pump_ctl_stop(sh_pump_ctl_t *c, uint32_t now_s)
{
    if (c->test) {
        c->test_end_s = now_s;
        test_min_length(c);
    }
}
