/* Created by Aram Vartanyan (c) 2026 */

/* Host test of the alarm and information codes (main/sh_alarm.c; doc/operating-modes.md
 * 5.2): E1, E2, E4, E5 and Hot from the real pump control (sh_pump_ctl), E6 with scripted
 * temperatures, the order of the codes on the display. 1 s clock; temperatures in tenths of °C. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_pump_ctl.h"
#include "sh_alarm.h"

#define BIT(a)  SH_ALARM_BIT(a)
#define MIN     60u

/* Defaults of Kconfig "Control" */
static const sh_pump_params_t P = {
    .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
    .tank_max = 700, .col_min = 250, .col_protect = 1100,
    .col_protect_cap = 1150, .col_boiling = 1270, .frost_on = true, .frost = 40,
};
static const sh_alarm_params_t A = {
    .e6_time_s = 600, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200, .e6_clear_diff = 100,
};

/* ---- with the pump control ---------------------------------------------------------------- */

typedef struct {
    sh_pump_ctl_t c;
    sh_pump_temps_t t;
    sh_alarm_state_t a;
    uint32_t now;
} sim_t;

static void sim_init(sim_t *s, int16_t col, int16_t tank)
{
    memset(s, 0, sizeof(*s));
    s->t.t_col = col;
    s->t.t_tank = tank;
}

static uint8_t step(sim_t *s)
{
    s->now++;
    sh_pump_ctl_step(&s->c, &P, &s->t, s->now);
    const sh_alarm_in_t in = {
        .t_col = s->t.t_col, .t_tank = s->t.t_tank,
        .col_fault = s->t.col_fault, .tank_fault = s->t.tank_fault,
        .pump_on = s->c.on, .reason = s->c.reason,
        .col_boiling = s->c.norm.col_boiling, .tank_ceiling = s->c.norm.tank_ceiling,
    };
    return sh_alarm_step(&s->a, &A, &in, s->now);
}

static uint8_t at(sim_t *s, int16_t col, int16_t tank)
{
    s->t.t_col = col;
    s->t.t_tank = tank;
    return step(s);
}

static void test_sensor_faults(void)
{
    sim_t s;
    sim_init(&s, 300, 300);
    CHECK_EQ(step(&s), 0);
    s.t.col_fault = true;
    CHECK_EQ(step(&s), BIT(SH_ALARM_E1));
    s.t.tank_fault = true;
    CHECK_EQ(step(&s), BIT(SH_ALARM_E1) | BIT(SH_ALARM_E2));
    s.t.col_fault = false;
    CHECK_EQ(step(&s), BIT(SH_ALARM_E2));
    s.t.tank_fault = false;
    CHECK_EQ(step(&s), 0);
    CHECK_EQ(s.a.active, 0);
}

static void test_e4(void)
{
    sim_t s;
    sim_init(&s, 300, 900);
    CHECK_EQ(at(&s, 300, 949), 0);
    CHECK_EQ(at(&s, 300, 950), BIT(SH_ALARM_E4));       /* >= 95 °C */
    CHECK_EQ(at(&s, 300, 930), BIT(SH_ALARM_E4));       /* the return zone */
    CHECK_EQ(at(&s, 300, 929), 0);                      /* below 93 °C */
    CHECK_EQ(at(&s, 300, 950), BIT(SH_ALARM_E4));
    s.t.tank_fault = true;                              /* the limit cannot be checked */
    CHECK_EQ(step(&s), BIT(SH_ALARM_E2));
    s.t.tank_fault = false;
    CHECK_EQ(step(&s), BIT(SH_ALARM_E4));
}

static void test_e5(void)
{
    sim_t s;
    sim_init(&s, 1200, 600);
    CHECK_EQ(at(&s, 1269, 600) & BIT(SH_ALARM_E5), 0);
    CHECK_EQ(at(&s, 1270, 600) & BIT(SH_ALARM_E5), BIT(SH_ALARM_E5));
    CHECK_EQ(at(&s, 1250, 600) & BIT(SH_ALARM_E5), BIT(SH_ALARM_E5));
    CHECK_EQ(at(&s, 1249, 600) & BIT(SH_ALARM_E5), 0);
    CHECK_EQ(at(&s, 1270, 600) & BIT(SH_ALARM_E5), BIT(SH_ALARM_E5));
    s.t.col_fault = true;
    CHECK_EQ(step(&s), BIT(SH_ALARM_E1));
}

static void test_hot(void)
{
    sim_t s;
    /* tank at Tmax: only the collector protection can run the pump */
    sim_init(&s, 1000, 720);
    CHECK_EQ(step(&s), 0);
    CHECK_EQ(at(&s, 1099, 720), 0);
    CHECK_EQ(at(&s, 1100, 720), BIT(SH_ALARM_HOT));
    CHECK(s.c.on);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_COL_PROTECT);
    for (int i = 0; i < 300; i++) {
        at(&s, 1079, 720);                              /* below 108 °C, after the minimum run */
    }
    CHECK(!s.c.on);
    CHECK_EQ(s.a.active, 0);

    /* the test is not a protection: no Hot */
    sim_init(&s, 1000, 720);
    step(&s);
    sh_pump_ctl_plus(&s.c, 300, s.now);
    CHECK_EQ(step(&s), 0);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_TEST);

    /* tank sensor faulty: the probe at the collector emergency temperature */
    sim_init(&s, 1000, 500);
    s.t.tank_fault = true;
    CHECK_EQ(step(&s), BIT(SH_ALARM_E2));
    CHECK_EQ(at(&s, 1100, 500), BIT(SH_ALARM_E2) | BIT(SH_ALARM_HOT));
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_TF_PROBE);
    for (uint32_t i = 0; i < P.min_run_s; i++) {
        at(&s, 960, 500);                               /* still >= 93 °C at the end: failed */
    }
    CHECK(!s.c.on);
    CHECK_EQ(s.a.active, BIT(SH_ALARM_E2));
}

/* ---- E6 with scripted inputs ------------------------------------------------------------- */

typedef struct {
    sh_alarm_state_t a;
    sh_alarm_in_t in;
    uint32_t now;
    uint32_t start;     /* pump start */
} e6_t;

static void e6_init(e6_t *e)
{
    memset(e, 0, sizeof(*e));
    e->in.t_col = 400;
    e->in.t_tank = 300;
    e->in.reason = SH_PUMP_REASON_DIFF;
}

static void e6_pump(e6_t *e, bool on, sh_pump_reason_t reason)
{
    if (on && !e->in.pump_on) {
        e->start = e->now + 1;  /* the next step is the first one with the pump on */
    }
    e->in.pump_on = on;
    e->in.reason = reason;
}

static bool e6_step(e6_t *e)
{
    e->now++;
    return (sh_alarm_step(&e->a, &A, &e->in, e->now) & BIT(SH_ALARM_E6)) != 0;
}

/* Runs to `t` s after the pump start with linear temperatures: the collector rises `col_per_min`
 * and the tank `tank_per_min` tenths a minute (from the values at the start), after an optional
 * collector dip during the first 10 min. Returns E6 at the last step. */
static bool e6_run(e6_t *e, uint32_t t, int16_t col0, int16_t tank0, int col_per_min,
                   int tank_per_min)
{
    bool e6 = false;
    while (e->now < e->start + t) {
        const uint32_t s = e->now + 1 - e->start;
        e->in.t_col = (int16_t)(col0 + col_per_min * (int)s / 60);
        e->in.t_tank = (int16_t)(tank0 + tank_per_min * (int)s / 60);
        e6 = e6_step(e);
    }
    return e6;
}

/* Runs to the next check whose whole window is still ahead and sets the values at its two ends
 * (samples are taken at whole minutes after the start); returns E6 after that check. */
static bool e6_window(e6_t *e, int16_t col_from, int16_t col_to, int16_t tank_from, int16_t tank_to)
{
    /* the first check whose window starts after now (>= 20 min after the start) */
    uint32_t check = e->start + 20 * MIN;
    while (check - 10 * MIN <= e->now) {
        check += MIN;
    }
    const uint32_t from = check - 10 * MIN;
    bool e6 = false;
    while (e->now < check) {
        const bool at_from = e->now + 1 == from;
        const bool at_to = e->now + 1 == check;
        if (at_from) {
            e->in.t_col = col_from;
            e->in.t_tank = tank_from;
        } else if (at_to) {
            e->in.t_col = col_to;
            e->in.t_tank = tank_to;
        } else if (e->now + 1 > from) {
            e->in.t_col = (int16_t)((col_from + col_to) / 2);   /* in between: no influence */
            e->in.t_tank = (int16_t)((tank_from + tank_to) / 2);
        }
        e6 = e6_step(e);
    }
    return e6;
}

static void test_e6_declare(void)
{
    e6_t e;
    /* failed pump: the collector +2 K/min, the tank stays; the difference grows from 6 K */
    e6_init(&e);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    CHECK(!e6_run(&e, 20 * MIN - 1, 460, 400, 20, 0));  /* 1 s before the first check */
    CHECK(e6_run(&e, 20 * MIN, 460, 400, 20, 0));       /* the first check: 20 min after start */
    CHECK_EQ(e.a.active, BIT(SH_ALARM_E6));

    /* the thresholds, +-0.1 K: collector rise 10 K, tank rise < 0.5 K, difference 20 K */
    struct { int16_t c0, c1, t0, t1; bool e6; } cases[] = {
        { 600, 700, 400, 404, true },       /* all at the limits */
        { 600, 699, 400, 404, false },      /* collector rose 9.9 K */
        { 600, 700, 400, 405, false },      /* tank rose 0.5 K */
        { 600, 700, 500, 500, true },       /* difference 20.0 K */
        { 600, 700, 501, 501, false },      /* difference 19.9 K */
        { 600, 700, 400, 380, true },       /* the tank falls */
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        e6_init(&e);
        e6_step(&e);
        e6_pump(&e, true, SH_PUMP_REASON_DIFF);
        e6_run(&e, 9 * MIN, 500, cases[i].t0, 0, 0);
        const bool got = e6_window(&e, cases[i].c0, cases[i].c1, cases[i].t0, cases[i].t1);
        if (got != cases[i].e6) {
            printf("  E6 case %u\n", i);
        }
        CHECK_EQ(got, cases[i].e6);
    }

    /* the minimum run time and the collector protection are the normal control too */
    e6_init(&e);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_MIN_RUN);
    CHECK(e6_run(&e, 20 * MIN, 460, 400, 20, 0));
    e6_init(&e);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_COL_PROTECT);
    CHECK(e6_run(&e, 20 * MIN, 1100, 700, 20, 0));
}

static void test_e6_first_minutes(void)
{
    /* The first 10 min are skipped: the dip from the cold pipe liquid and the fast return are not in
     * any window. Collector 60 -> 45 °C in 5 min, back to 63 °C at 10 min, then +0.5 K/min. */
    e6_t e;
    e6_init(&e);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    bool any = false;
    while (e.now < e.start + 40 * MIN) {
        const uint32_t s = e.now + 1 - e.start;
        int col;
        if (s <= 5 * MIN) {
            col = 600 - 150 * (int)s / (5 * MIN);
        } else if (s <= 10 * MIN) {
            col = 450 + 180 * (int)(s - 5 * MIN) / (5 * MIN);
        } else {
            col = 630 + 5 * (int)(s - 10 * MIN) / 60;
        }
        e.in.t_col = (int16_t)col;
        e.in.t_tank = 400;
        any |= e6_step(&e);
    }
    CHECK(!any);

    /* The same dip, but a window allowed into the first minutes (e.g. 2 - 12 min) would see
     * 45 -> 63 °C = +18 K: here the first window is 10 - 20 min. */
    e6_init(&e);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    any = false;
    while (e.now < e.start + 20 * MIN) {
        const uint32_t s = e.now + 1 - e.start;
        e.in.t_col = (int16_t)(s <= 2 * MIN ? 600 - 75 * (int)s / 60 : s <= 12 * MIN ? 450 + 18 * (int)(s - 2 * MIN) / 60 : 630);
        e.in.t_tank = 400;
        any |= e6_step(&e);
    }
    CHECK(!any);
}

static void test_e6_exceptions(void)
{
    e6_t e;
    const sh_pump_reason_t not_normal[] = {
        SH_PUMP_REASON_TEST, SH_PUMP_REASON_ANTI_SEIZE, SH_PUMP_REASON_TF_RUN,
        SH_PUMP_REASON_TF_PROBE, SH_PUMP_REASON_CF_PROBE, SH_PUMP_REASON_CF_RUN,
    };
    for (unsigned i = 0; i < sizeof(not_normal) / sizeof(not_normal[0]); i++) {
        e6_init(&e);
        e6_step(&e);
        e6_pump(&e, true, not_normal[i]);
        CHECK(!e6_run(&e, 40 * MIN, 460, 400, 20, 0));
    }

    /* a faulty sensor: no check */
    e6_init(&e);
    e6_step(&e);
    e.in.tank_fault = true;
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    CHECK(!e6_run(&e, 40 * MIN, 460, 400, 20, 0));
    e6_init(&e);
    e6_step(&e);
    e.in.col_fault = true;
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    CHECK(!e6_run(&e, 40 * MIN, 460, 400, 20, 0));

    /* the run must be continuous: a stop starts the skipped time again */
    e6_init(&e);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    CHECK(!e6_run(&e, 19 * MIN, 460, 400, 20, 0));
    e6_pump(&e, false, SH_PUMP_REASON_DIFF);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    CHECK(!e6_run(&e, 20 * MIN - 1, 840, 400, 20, 0));
    CHECK(e6_run(&e, 20 * MIN, 840, 400, 20, 0));

    /* the test ends and the pump goes on by the normal control: the run counts from then */
    e6_init(&e);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_TEST);
    CHECK(!e6_run(&e, 5 * MIN, 460, 400, 20, 0));
    e.in.reason = SH_PUMP_REASON_DIFF;
    const uint32_t from = e.now + 1;
    e.start = from;
    CHECK(!e6_run(&e, 20 * MIN - 1, 560, 400, 20, 0));
    CHECK(e6_run(&e, 20 * MIN, 560, 400, 20, 0));
}

/* E6 active at the 20 min check of a run that started at 46 °C */
static void e6_declared(e6_t *e)
{
    e6_init(e);
    e6_step(e);
    e6_pump(e, true, SH_PUMP_REASON_DIFF);
    e6_run(e, 20 * MIN, 460, 400, 20, 0);
    CHECK(e->a.e6);
}

static void test_e6_clear(void)
{
    e6_t e;
    /* the tank rose 0.5 K over the window */
    e6_declared(&e);
    CHECK(e6_window(&e, 900, 920, 400, 404));       /* 0.4 K: stays */
    CHECK(!e6_window(&e, 900, 920, 400, 405));      /* 0.5 K: cleared */
    CHECK_EQ(e.a.active, 0);

    /* the difference below 10 K */
    e6_declared(&e);
    CHECK(e6_window(&e, 500, 500, 400, 400));       /* 10.0 K: stays */
    CHECK(!e6_window(&e, 499, 499, 400, 400));      /* 9.9 K: cleared */

    /* only at the checks: a low difference in between does not clear */
    e6_declared(&e);
    e.in.t_col = 401;
    e.in.t_tank = 400;
    for (int i = 0; i < 59; i++) {
        CHECK(e6_step(&e));
    }

    /* the pump stops, test, anti-seize, sensor faults: E6 stays (until restart) */
    e6_declared(&e);
    e6_pump(&e, false, SH_PUMP_REASON_DIFF);
    e.in.t_col = 300;
    e.in.t_tank = 400;
    for (int i = 0; i < 3600; i++) {
        e6_step(&e);
    }
    CHECK(e.a.e6);
    e6_pump(&e, true, SH_PUMP_REASON_TEST);
    CHECK(e6_run(&e, 40 * MIN, 410, 400, 0, 10));
    e6_pump(&e, false, SH_PUMP_REASON_DIFF);
    e6_step(&e);
    e.in.tank_fault = true;
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    CHECK(e6_run(&e, 40 * MIN, 410, 400, 0, 10));
    e.in.tank_fault = false;

    /* a new normal run: before its first check E6 stays, then the check clears it */
    e6_pump(&e, false, SH_PUMP_REASON_DIFF);
    e6_step(&e);
    e6_pump(&e, true, SH_PUMP_REASON_DIFF);
    CHECK(e6_run(&e, 20 * MIN - 1, 700, 400, 0, 1));
    CHECK(!e6_run(&e, 20 * MIN, 700, 400, 0, 1));  /* the tank rose 1 K over the window */
}

static void test_e6_window_param(void)
{
    /* a 5 min Kconfig time: first check after 10 min, window 5 min */
    const sh_alarm_params_t a5 = {
        .e6_time_s = 300, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200, .e6_clear_diff = 100,
    };
    sh_alarm_state_t st;
    memset(&st, 0, sizeof(st));
    sh_alarm_in_t in = { .t_col = 460, .t_tank = 400, .pump_on = true, .reason = SH_PUMP_REASON_DIFF };
    bool e6 = false;
    uint32_t first = 0;
    for (uint32_t s = 1; s <= 20 * MIN && !first; s++) {
        in.t_col = (int16_t)(460 + 20 * (int)s / 60);
        e6 = sh_alarm_step(&st, &a5, &in, s) & BIT(SH_ALARM_E6);
        if (e6) {
            first = s;
        }
    }
    CHECK_EQ(first, 1 + 10 * MIN);      /* the run started at 1 s */
}

static void test_fro(void)
{
    sim_t s;
    /* collector below the frost threshold (4.0 °C), tank warm: the pump runs by the frost protection */
    sim_init(&s, 40, 300);
    CHECK_EQ(step(&s), 0);
    CHECK_EQ(at(&s, 39, 300), BIT(SH_ALARM_FRO));
    CHECK(s.c.on);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_FROST);
    for (int i = 0; i < 300; i++) {
        CHECK_EQ(at(&s, 59, 300), BIT(SH_ALARM_FRO));   /* below the threshold + 2 K */
    }
    CHECK_EQ(at(&s, 60, 300), 0);                       /* the protection ended: cleared */
    CHECK(!s.c.on);

    /* tank below 5 °C: the protection does not run, no code */
    sim_init(&s, 20, 49);
    CHECK_EQ(step(&s), 0);

    /* only while the pump runs by it: not during the pump test */
    sim_init(&s, 20, 300);
    CHECK_EQ(step(&s), BIT(SH_ALARM_FRO));
    sh_pump_ctl_plus(&s.c, 300, s.now);
    CHECK_EQ(step(&s), 0);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_TEST);

    /* a faulty sensor: not checked, only its own code */
    sim_init(&s, 20, 300);
    s.t.tank_fault = true;
    CHECK_EQ(step(&s), BIT(SH_ALARM_E2));

    /* E6 is not checked during the frost protection (not the normal control) */
    sim_init(&s, 20, 300);
    for (int i = 0; i < 30 * 60; i++) {
        CHECK_EQ(step(&s), BIT(SH_ALARM_FRO));
    }
}

static void test_display_order(void)
{
    const uint8_t all = BIT(SH_ALARM_E1) | BIT(SH_ALARM_E2) | BIT(SH_ALARM_E4) | BIT(SH_ALARM_E5) |
                        BIT(SH_ALARM_E6) | BIT(SH_ALARM_HOT) | BIT(SH_ALARM_FRO);
    CHECK_EQ(sh_alarm_code_at(all, 0), SH_ALARM_E1);
    CHECK_EQ(sh_alarm_code_at(all, 999), SH_ALARM_E1);
    CHECK_EQ(sh_alarm_code_at(all, 1000), SH_ALARM_E2);
    CHECK_EQ(sh_alarm_code_at(all, 2000), SH_ALARM_E4);
    CHECK_EQ(sh_alarm_code_at(all, 3000), SH_ALARM_E5);
    CHECK_EQ(sh_alarm_code_at(all, 4000), SH_ALARM_E6);
    CHECK_EQ(sh_alarm_code_at(all, 5999), SH_ALARM_HOT);
    CHECK_EQ(sh_alarm_code_at(all, 6000), SH_ALARM_FRO);     /* Fro after Hot */
    CHECK_EQ(sh_alarm_code_at(all, 6999), SH_ALARM_FRO);
    CHECK_EQ(sh_alarm_code_at(all, 7000), SH_ALARM_COUNT);
    CHECK_EQ(sh_alarm_code_at(BIT(SH_ALARM_E1) | BIT(SH_ALARM_FRO), 1000), SH_ALARM_FRO);

    /* the information after the alarms, the alarms by number */
    const uint8_t some = BIT(SH_ALARM_HOT) | BIT(SH_ALARM_E6) | BIT(SH_ALARM_E2);
    CHECK_EQ(sh_alarm_code_at(some, 0), SH_ALARM_E2);
    CHECK_EQ(sh_alarm_code_at(some, 1000), SH_ALARM_E6);
    CHECK_EQ(sh_alarm_code_at(some, 2000), SH_ALARM_HOT);
    CHECK_EQ(sh_alarm_code_at(some, 2999), SH_ALARM_HOT);
    CHECK_EQ(sh_alarm_code_at(some, 3000), SH_ALARM_COUNT);

    CHECK_EQ(sh_alarm_code_at(0, 0), SH_ALARM_COUNT);

    CHECK(strcmp(sh_alarm_name(SH_ALARM_E1), "E1") == 0);
    CHECK(strcmp(sh_alarm_name(SH_ALARM_HOT), "Hot") == 0);
    CHECK(strcmp(sh_alarm_name(SH_ALARM_FRO), "Fro") == 0);
    CHECK(strcmp(sh_alarm_name(SH_ALARM_COUNT), "?") == 0);
}

int main(void)
{
    test_sensor_faults();
    test_e4();
    test_e5();
    test_hot();
    test_fro();
    test_e6_declare();
    test_e6_first_minutes();
    test_e6_exceptions();
    test_e6_clear();
    test_e6_window_param();
    test_display_order();
    return test_summary("test_alarm");
}
