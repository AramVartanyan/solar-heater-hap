/* Host test of the alarms and the information (main/sh_alarms.c): E1, E2, E4, E5, E6, Hot, their
 * order and the normal screen of the display (doc/tasks/step5-alarms-indication.md 3.1, 3.2). A
 * simulation with a 1 s clock, as the control task runs it; the last part runs it together with
 * the pump control (sh_pump_ctl). Temperatures are tenths of °C, times are seconds. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_pump_ctl.h"
#include "sh_alarms.h"

/* Defaults of the task (Kconfig menu "Control"; doc/led-indication.md p. 6) */
static const sh_alarm_params_t A = {
    .window_s = 600,
    .col_rise = 100,
    .tank_rise = 5,
    .diff = 200,
    .clear_diff = 100,
};

#define INV     SH_TEMP_INVALID
#define E1      SH_ALARM_BIT(SH_ALARM_E1)
#define E2      SH_ALARM_BIT(SH_ALARM_E2)
#define E4      SH_ALARM_BIT(SH_ALARM_E4)
#define E5      SH_ALARM_BIT(SH_ALARM_E5)
#define E6      SH_ALARM_BIT(SH_ALARM_E6)
#define HOT     SH_ALARM_BIT(SH_ALARM_HOT)

/* ---- the alarms alone: the pump state is given ---- */

typedef struct {
    sh_alarms_t a;
    sh_alarm_in_t in;
    uint32_t now;
    /* temperatures as straight lines: base + rise per 600 s */
    int16_t col0, tank0;
    int col_rise, tank_rise;
    uint32_t ramp_s;
} sim_t;

/* Boot: pump off, no fault. */
static sim_t sim(int16_t col, int16_t tank)
{
    sim_t s;
    memset(&s, 0, sizeof(s));
    s.in.t_col = s.col0 = col;
    s.in.t_tank = s.tank0 = tank;
    return s;
}

/* From now on the temperatures change by this much every 600 s. */
static void ramp(sim_t *s, int col_rise, int tank_rise)
{
    s->col0 = s->in.t_col;
    s->tank0 = s->in.t_tank;
    s->col_rise = col_rise;
    s->tank_rise = tank_rise;
    s->ramp_s = 0;
}

/* Steady temperatures from now on. */
static void hold(sim_t *s, int16_t col, int16_t tank)
{
    s->in.t_col = col;
    s->in.t_tank = tank;
    ramp(s, 0, 0);
}

static void pump(sim_t *s, bool on, sh_pump_reason_t reason)
{
    s->in.pump_on = on;
    s->in.pump_reason = reason;
}

/* A step at the current second (a change takes effect at once, as after a button command). */
static uint8_t step(sim_t *s)
{
    return sh_alarms_step(&s->a, &A, &s->in, s->now);
}

/* `n` steps of 1 s */
static void run(sim_t *s, uint32_t n)
{
    while (n--) {
        s->now++;
        s->ramp_s++;
        s->in.t_col = (int16_t)(s->col0 + s->col_rise * (int)s->ramp_s / 600);
        s->in.t_tank = (int16_t)(s->tank0 + s->tank_rise * (int)s->ramp_s / 600);
        step(s);
    }
}

/* The pump starts by the difference at `col` / `tank` and runs with these rises per 10 min. */
static sim_t run_start(int16_t col, int16_t tank, int col_rise, int tank_rise)
{
    sim_t s = sim(col, tank);
    run(&s, 30);
    pump(&s, true, SH_PUMP_REASON_DIFF);
    step(&s);
    ramp(&s, col_rise, tank_rise);
    return s;
}

/* E6 declared: collector 50 -> 60 °C in the first 10 min of the run, tank steady at 30 °C. */
static sim_t e6_active(void)
{
    sim_t s = run_start(500, 300, 100, 0);
    run(&s, 600);
    CHECK_EQ(s.a.active, E6);
    hold(&s, 600, 300);
    return s;
}

static void test_sensor_faults(void)
{
    sim_t s = sim(400, 300);
    run(&s, 10);
    CHECK_EQ(s.a.active, 0);

    s.in.col_fault = true;                  /* E1 as long as the sensor is faulty */
    run(&s, 1);
    CHECK_EQ(s.a.active, E1);
    s.in.tank_fault = true;
    run(&s, 1);
    CHECK_EQ(s.a.active, E1 | E2);
    s.in.col_fault = false;
    run(&s, 1);
    CHECK_EQ(s.a.active, E2);
    s.in.tank_fault = false;
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);

    /* no value since boot is not a fault yet */
    s = sim(INV, INV);
    run(&s, 10);
    CHECK_EQ(s.a.active, 0);
}

static void test_e4_tank_ceiling(void)
{
    sim_t s = sim(400, 949);
    run(&s, 5);
    CHECK_EQ(s.a.active, 0);                /* 94.9 °C */
    hold(&s, 400, 950);
    run(&s, 1);
    CHECK_EQ(s.a.active, E4);               /* 95.0 °C */
    hold(&s, 400, 930);
    run(&s, 5);
    CHECK_EQ(s.a.active, E4);               /* 93.0 °C: still */
    hold(&s, 400, 929);
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);                /* 92.9 °C */
    hold(&s, 400, 949);
    run(&s, 5);
    CHECK_EQ(s.a.active, 0);                /* back in the return zone from below: not active */

    /* not with a faulty tank sensor: E2 only; the limit is remembered */
    hold(&s, 400, 960);
    run(&s, 1);
    CHECK_EQ(s.a.active, E4);
    s.in.tank_fault = true;
    run(&s, 1);
    CHECK_EQ(s.a.active, E2);
    s.in.tank_fault = false;
    hold(&s, 400, 940);                     /* recovered inside the return zone: still active */
    run(&s, 1);
    CHECK_EQ(s.a.active, E4);
    hold(&s, 400, 929);
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);

    /* the last value of a faulty sensor is not used */
    s.in.tank_fault = true;
    hold(&s, 400, 960);
    run(&s, 5);
    CHECK_EQ(s.a.active, E2);
    hold(&s, 400, 900);
    s.in.tank_fault = false;
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);
}

static void test_e5_col_boiling(void)
{
    sim_t s = sim(1269, 500);
    run(&s, 5);
    CHECK_EQ(s.a.active, 0);                /* 126.9 °C */
    hold(&s, 1270, 500);
    run(&s, 1);
    CHECK_EQ(s.a.active, E5);               /* 127.0 °C */
    hold(&s, 1250, 500);
    run(&s, 5);
    CHECK_EQ(s.a.active, E5);               /* 125.0 °C: still */
    hold(&s, 1249, 500);
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);                /* 124.9 °C */

    /* not with a faulty collector sensor: E1 only; the limit is remembered */
    hold(&s, 1280, 500);
    run(&s, 1);
    CHECK_EQ(s.a.active, E5);
    s.in.col_fault = true;
    run(&s, 1);
    CHECK_EQ(s.a.active, E1);
    s.in.col_fault = false;
    hold(&s, 1260, 500);
    run(&s, 1);
    CHECK_EQ(s.a.active, E5);
    hold(&s, 1249, 500);
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);

    /* the last value of a faulty sensor is not used */
    s.in.col_fault = true;
    hold(&s, 1280, 500);
    run(&s, 5);
    CHECK_EQ(s.a.active, E1);

    /* both limits and both faults at once */
    s = sim(1270, 950);
    run(&s, 1);
    CHECK_EQ(s.a.active, E4 | E5);
    s.in.col_fault = true;
    s.in.tank_fault = true;
    run(&s, 1);
    CHECK_EQ(s.a.active, E1 | E2);
}

static void test_hot(void)
{
    sim_t s = sim(1100, 600);
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);

    pump(&s, true, SH_PUMP_REASON_COL_PROTECT);     /* the collector protection runs the pump */
    run(&s, 1);
    CHECK_EQ(s.a.active, HOT);
    pump(&s, false, SH_PUMP_REASON_TANK_CEILING);
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);

    s.in.tank_fault = true;                 /* the probe at the collector emergency temperature */
    pump(&s, true, SH_PUMP_REASON_TF_PROBE);
    run(&s, 1);
    CHECK_EQ(s.a.active, E2 | HOT);
    pump(&s, false, SH_PUMP_REASON_TF_HOT);
    run(&s, 1);
    CHECK_EQ(s.a.active, E2);
    s.in.tank_fault = false;

    /* every other reason, pump on or off: no Hot */
    for (int r = 0; r < SH_PUMP_REASON_COUNT; r++) {
        if (r == SH_PUMP_REASON_COL_PROTECT || r == SH_PUMP_REASON_TF_PROBE) {
            continue;
        }
        pump(&s, true, (sh_pump_reason_t)r);
        CHECK_EQ(step(&s) & HOT, 0);
    }
    pump(&s, false, SH_PUMP_REASON_COL_PROTECT);    /* not without the pump */
    CHECK_EQ(step(&s), 0);
}

static void test_e6_declared(void)
{
    /* collector +10.0 K, tank +0.4 K in 10 min, difference 25.6 K: after 10 min of running */
    sim_t s = run_start(500, 300, 100, 4);
    run(&s, 599);
    CHECK_EQ(s.a.active, 0);                /* 9 min 59 s */
    run(&s, 1);
    CHECK_EQ(s.a.active, E6);               /* 10 min */
    CHECK_EQ(s.in.t_col, 600);
    CHECK_EQ(s.in.t_tank, 304);

    /* collector +9.9 K: never */
    s = run_start(500, 300, 99, 0);
    run(&s, 3600);
    CHECK_EQ(s.a.active, 0);

    /* tank +0.5 K: never */
    s = run_start(500, 300, 100, 5);
    run(&s, 3600);
    CHECK_EQ(s.a.active, 0);

    /* difference 19.9 K after 10 min: not yet; 20.0 K at the next sample */
    s = run_start(400, 301, 100, 0);
    run(&s, 600);
    CHECK_EQ(s.in.t_col - s.in.t_tank, 199);
    CHECK_EQ(s.a.active, 0);
    run(&s, 9);
    CHECK_EQ(s.a.active, 0);
    run(&s, 1);
    CHECK_EQ(s.in.t_col - s.in.t_tank, 200);
    CHECK_EQ(s.a.active, E6);

    /* difference exactly 20.0 K after 10 min */
    s = run_start(400, 300, 100, 0);
    run(&s, 599);
    CHECK_EQ(s.a.active, 0);
    run(&s, 1);
    CHECK_EQ(s.a.active, E6);

    /* the fast rise comes later in the run: declared 10 min after it began */
    s = run_start(600, 300, 0, 0);
    run(&s, 1800);
    CHECK_EQ(s.a.active, 0);
    ramp(&s, 100, 0);
    run(&s, 590);
    CHECK_EQ(s.a.active, 0);
    run(&s, 10);
    CHECK_EQ(s.a.active, E6);

    /* the pump is not running: nothing, whatever the temperatures do */
    s = sim(500, 300);
    ramp(&s, 100, 0);
    run(&s, 3600);
    CHECK_EQ(s.a.active, 0);
}

static void test_e6_run(void)
{
    /* the minimum run time and the collector protection are the normal control too */
    sim_t s = run_start(1000, 600, 100, 0);
    run(&s, 200);
    pump(&s, true, SH_PUMP_REASON_MIN_RUN);
    run(&s, 200);
    pump(&s, true, SH_PUMP_REASON_COL_PROTECT);
    run(&s, 199);
    CHECK_EQ(s.a.active, HOT);
    run(&s, 1);
    CHECK_EQ(s.a.active, E6 | HOT);

    /* not the pump test, not the anti-seize run */
    s = sim(500, 300);
    pump(&s, true, SH_PUMP_REASON_TEST);
    step(&s);
    ramp(&s, 100, 0);
    run(&s, 900);
    CHECK_EQ(s.a.active, 0);
    /* the pump goes on by the difference after the test: 10 min from then */
    pump(&s, true, SH_PUMP_REASON_DIFF);
    run(&s, 599);
    CHECK_EQ(s.a.active, 0);
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);                /* the run began 1 s after the last test step */
    run(&s, 1);
    CHECK_EQ(s.a.active, E6);

    s = sim(500, 300);
    pump(&s, true, SH_PUMP_REASON_ANTI_SEIZE);
    step(&s);
    ramp(&s, 100, 0);
    run(&s, 3600);
    CHECK_EQ(s.a.active, 0);

    /* not with a faulty sensor (the runs and the probes of the fault modes) */
    static const sh_pump_reason_t tank_fault[] = { SH_PUMP_REASON_TF_RUN, SH_PUMP_REASON_TF_PROBE,
                                                   SH_PUMP_REASON_MIN_RUN };
    for (unsigned i = 0; i < sizeof(tank_fault) / sizeof(tank_fault[0]); i++) {
        s = sim(700, 300);
        s.in.tank_fault = true;
        pump(&s, true, tank_fault[i]);
        step(&s);
        ramp(&s, 100, 0);
        run(&s, 3600);
        CHECK_EQ(s.a.active & E6, 0);
    }
    static const sh_pump_reason_t col_fault[] = { SH_PUMP_REASON_CF_PROBE, SH_PUMP_REASON_CF_RUN };
    for (unsigned i = 0; i < sizeof(col_fault) / sizeof(col_fault[0]); i++) {
        s = sim(700, 300);
        s.in.col_fault = true;
        pump(&s, true, col_fault[i]);
        step(&s);
        ramp(&s, 100, 0);
        run(&s, 3600);
        CHECK_EQ(s.a.active, E1);
    }

    /* a sensor fault in the run: the check starts again when the sensor is back */
    s = run_start(500, 300, 100, 0);
    run(&s, 300);
    s.in.tank_fault = true;
    pump(&s, true, SH_PUMP_REASON_TF_RUN);
    run(&s, 60);
    s.in.tank_fault = false;
    pump(&s, true, SH_PUMP_REASON_DIFF);
    run(&s, 1);                             /* the run starts again here */
    run(&s, 599);
    CHECK_EQ(s.a.active, 0);
    run(&s, 1);
    CHECK_EQ(s.a.active, E6);

    /* a break of the run: 10 min from the new start */
    s = run_start(500, 300, 100, 0);
    run(&s, 590);
    pump(&s, false, SH_PUMP_REASON_DIFF);
    run(&s, 1);
    pump(&s, true, SH_PUMP_REASON_DIFF);
    run(&s, 1);                             /* the new run starts here */
    run(&s, 599);
    CHECK_EQ(s.a.active, 0);
    run(&s, 1);
    CHECK_EQ(s.a.active, E6);
}

static void test_e6_cleared(void)
{
    /* the tank rises 0.4 K in 10 min: stays */
    sim_t s = e6_active();
    ramp(&s, 0, 4);
    run(&s, 3600);
    CHECK_EQ(s.a.active, E6);

    /* the tank rises 0.5 K in 10 min: cleared (there is flow) */
    s = e6_active();
    ramp(&s, 0, 5);
    run(&s, 599);
    CHECK_EQ(s.a.active, E6);
    run(&s, 1);
    CHECK_EQ(s.in.t_tank, 305);
    CHECK_EQ(s.a.active, 0);

    /* the difference 10.0 K: stays; 9.9 K: cleared */
    s = e6_active();
    hold(&s, 400, 300);
    run(&s, 600);
    CHECK_EQ(s.a.active, E6);
    hold(&s, 399, 300);
    run(&s, 10);                            /* by the next sample */
    CHECK_EQ(s.a.active, 0);

    /* not declared again at once: the collector must rise 10 K in 10 min again */
    run(&s, 600);
    CHECK_EQ(s.a.active, 0);

    /* nothing changes: stays; the pump keeps running */
    s = e6_active();
    run(&s, 4 * 3600);
    CHECK_EQ(s.a.active, E6);

    /* the pump stops: stays, also with a low difference and a rising tank (the heater) */
    s = e6_active();
    pump(&s, false, SH_PUMP_REASON_DIFF);
    hold(&s, 200, 300);
    ramp(&s, 0, 50);
    run(&s, 3600);
    CHECK_EQ(s.a.active, E6);
    ramp(&s, 0, 0);
    run(&s, 12 * 3600);
    CHECK_EQ(s.a.active, E6);

    /* the next run: judged only after 10 min of it */
    hold(&s, 370, 300);
    pump(&s, true, SH_PUMP_REASON_DIFF);
    run(&s, 1);                             /* the run starts here */
    run(&s, 599);
    CHECK_EQ(s.a.active, E6);
    run(&s, 1);
    CHECK_EQ(s.a.active, 0);                /* difference 7 K with the pump running: flow */

    /* the pump test and the anti-seize run do not clear it */
    s = e6_active();
    pump(&s, true, SH_PUMP_REASON_TEST);
    hold(&s, 350, 300);
    ramp(&s, 0, 20);
    run(&s, 900);
    CHECK_EQ(s.a.active, E6);
    pump(&s, true, SH_PUMP_REASON_ANTI_SEIZE);
    run(&s, 900);
    CHECK_EQ(s.a.active, E6);

    /* a sensor fault: E6 stays next to it, and is not judged */
    s = e6_active();
    s.in.col_fault = true;
    pump(&s, true, SH_PUMP_REASON_CF_RUN);
    ramp(&s, 0, 20);
    run(&s, 3600);
    CHECK_EQ(s.a.active, E1 | E6);

    /* E6 next to the limits */
    s = e6_active();
    pump(&s, false, SH_PUMP_REASON_COL_BOILING);
    hold(&s, 1270, 300);
    run(&s, 1);
    CHECK_EQ(s.a.active, E5 | E6);
}

/* Other thresholds than the defaults: the window sets the sample period too. */
static void test_e6_params(void)
{
    const sh_alarm_params_t p = { .window_s = 300, .col_rise = 50, .tank_rise = 10, .diff = 150,
                                  .clear_diff = 80 };
    sh_alarms_t a;
    memset(&a, 0, sizeof(a));
    sh_alarm_in_t in = { .t_col = 500, .t_tank = 350, .pump_on = true,
                         .pump_reason = SH_PUMP_REASON_DIFF };
    uint32_t now = 1000;
    sh_alarms_step(&a, &p, &in, now);
    for (int i = 1; i <= 299; i++) {
        in.t_col = (int16_t)(500 + 50 * i / 300);
        sh_alarms_step(&a, &p, &in, ++now);
    }
    CHECK_EQ(a.active, 0);
    in.t_col = 550;
    in.t_tank = 359;                        /* +0.9 K < 1.0 K */
    CHECK_EQ(sh_alarms_step(&a, &p, &in, ++now), E6);
    in.t_col = 439;                         /* difference 8.0 K: stays; 7.9 K: cleared */
    for (int i = 0; i < 5; i++) {
        sh_alarms_step(&a, &p, &in, ++now);
    }
    CHECK_EQ(a.active, E6);
    in.t_col = 438;
    for (int i = 0; i < 5; i++) {
        sh_alarms_step(&a, &p, &in, ++now);
    }
    CHECK_EQ(a.active, 0);
}

/* ---- the display: texts, order, the normal screen ---- */

static void test_texts(void)
{
    CHECK(strcmp(sh_alarm_text(SH_ALARM_E1), " E1") == 0);     /* right-aligned on 3 digits */
    CHECK(strcmp(sh_alarm_text(SH_ALARM_E2), " E2") == 0);
    CHECK(strcmp(sh_alarm_text(SH_ALARM_E4), " E4") == 0);
    CHECK(strcmp(sh_alarm_text(SH_ALARM_E5), " E5") == 0);
    CHECK(strcmp(sh_alarm_text(SH_ALARM_E6), " E6") == 0);
    CHECK(strcmp(sh_alarm_text(SH_ALARM_HOT), "Hot") == 0);     /* all 3 digits */
    CHECK(strcmp(sh_alarm_name(SH_ALARM_E4), "E4") == 0);
    CHECK(strcmp(sh_alarm_name(SH_ALARM_HOT), "Hot") == 0);
    for (int c = 0; c < SH_ALARM_COUNT; c++) {
        CHECK_EQ(strlen(sh_alarm_text((sh_alarm_t)c)), 3);
        CHECK(strcmp(sh_alarm_desc((sh_alarm_t)c), "?") != 0);
    }
}

/* What the screen shows at `ms`: 100 + the code for a code, otherwise the sh_screen_t. */
static int screen(uint8_t active, uint32_t ms)
{
    sh_alarm_t code = SH_ALARM_COUNT;
    const sh_screen_t scr = sh_alarm_screen(active, ms, &code);
    return scr == SH_SCREEN_CODE ? 100 + (int)code : (int)scr;
}

static void test_screen(void)
{
    /* no code: Boil 1 s, tank 5 s, Col 1 s, collector 5 s, again */
    CHECK_EQ(screen(0, 0), SH_SCREEN_BOIL);
    CHECK_EQ(screen(0, 999), SH_SCREEN_BOIL);
    CHECK_EQ(screen(0, 1000), SH_SCREEN_T_TANK);
    CHECK_EQ(screen(0, 5999), SH_SCREEN_T_TANK);
    CHECK_EQ(screen(0, 6000), SH_SCREEN_COL);
    CHECK_EQ(screen(0, 6999), SH_SCREEN_COL);
    CHECK_EQ(screen(0, 7000), SH_SCREEN_T_COL);
    CHECK_EQ(screen(0, 11999), SH_SCREEN_T_COL);
    CHECK_EQ(screen(0, 12000), SH_SCREEN_END);

    /* one code: 1 s after the standard cycle */
    CHECK_EQ(screen(E4, 11999), SH_SCREEN_T_COL);
    CHECK_EQ(screen(E4, 12000), 100 + SH_ALARM_E4);
    CHECK_EQ(screen(E4, 12999), 100 + SH_ALARM_E4);
    CHECK_EQ(screen(E4, 13000), SH_SCREEN_END);
    CHECK_EQ(screen(HOT, 12000), 100 + SH_ALARM_HOT);
    CHECK_EQ(screen(HOT, 13000), SH_SCREEN_END);

    /* several: the alarms by number, then the information, 1 s each */
    CHECK_EQ(screen(HOT | E6 | E2, 12000), 100 + SH_ALARM_E2);
    CHECK_EQ(screen(HOT | E6 | E2, 12999), 100 + SH_ALARM_E2);
    CHECK_EQ(screen(HOT | E6 | E2, 13000), 100 + SH_ALARM_E6);
    CHECK_EQ(screen(HOT | E6 | E2, 13999), 100 + SH_ALARM_E6);
    CHECK_EQ(screen(HOT | E6 | E2, 14000), 100 + SH_ALARM_HOT);
    CHECK_EQ(screen(HOT | E6 | E2, 14999), 100 + SH_ALARM_HOT);
    CHECK_EQ(screen(HOT | E6 | E2, 15000), SH_SCREEN_END);

    /* all of them */
    const uint8_t all = E1 | E2 | E4 | E5 | E6 | HOT;
    static const sh_alarm_t order[] = { SH_ALARM_E1, SH_ALARM_E2, SH_ALARM_E4, SH_ALARM_E5,
                                        SH_ALARM_E6, SH_ALARM_HOT };
    for (unsigned i = 0; i < 6; i++) {
        CHECK_EQ(screen(all, 12000 + 1000 * i), 100 + (int)order[i]);
        CHECK_EQ(screen(all, 12999 + 1000 * i), 100 + (int)order[i]);
    }
    CHECK_EQ(screen(all, 18000), SH_SCREEN_END);

    /* the standard part does not depend on the codes */
    CHECK_EQ(screen(all, 0), SH_SCREEN_BOIL);
    CHECK_EQ(screen(all, 1000), SH_SCREEN_T_TANK);
    CHECK_EQ(screen(all, 6000), SH_SCREEN_COL);
    CHECK_EQ(screen(all, 7000), SH_SCREEN_T_COL);
}

/* ---- together with the pump control, as in the control task ---- */

static const sh_pump_params_t P = {
    .diff_on = 60,
    .diff_off = 40,
    .min_run_s = 180,
    .min_off_s = 120,
    .tank_max = 700,
    .col_min = 250,
    .col_protect = 1100,
};

typedef struct {
    sh_pump_ctl_t c;
    sh_alarms_t a;
    sh_pump_temps_t t;
    uint32_t now;
} ctl_sim_t;

static ctl_sim_t ctl_sim(int16_t col, int16_t tank)
{
    ctl_sim_t s;
    memset(&s, 0, sizeof(s));
    s.t.t_col = col;
    s.t.t_tank = tank;
    return s;
}

static void ctl_step(ctl_sim_t *s)
{
    sh_pump_ctl_step(&s->c, &P, &s->t, s->now);
    const sh_alarm_in_t in = {
        .t_col = s->t.t_col, .t_tank = s->t.t_tank, .col_fault = s->t.col_fault,
        .tank_fault = s->t.tank_fault, .pump_on = s->c.on, .pump_reason = s->c.reason,
    };
    sh_alarms_step(&s->a, &A, &in, s->now);
    /* the alarms follow the same limits as the pump */
    if (!s->t.tank_fault) {
        CHECK_EQ((s->a.active & E4) != 0, s->c.norm.tank_ceiling);
    }
    if (!s->t.col_fault) {
        CHECK_EQ((s->a.active & E5) != 0, s->c.norm.col_boiling);
    }
}

static void ctl_run(ctl_sim_t *s, uint32_t n)
{
    while (n--) {
        s->now++;
        ctl_step(s);
    }
}

/* `n` seconds with the collector rising 0.1 K every `every_s` seconds */
static void ctl_heat(ctl_sim_t *s, uint32_t n, uint32_t every_s)
{
    for (uint32_t i = 1; i <= n; i++) {
        if (i % every_s == 0) {
            s->t.t_col++;
        }
        ctl_run(s, 1);
    }
}

static void test_with_pump_ctl(void)
{
    /* No flow: the pump starts by the difference, the collector runs away at 1 K/min, the tank
     * stands still. E6 after 10 min; the pump is not stopped by it. */
    ctl_sim_t s = ctl_sim(400, 300);
    ctl_run(&s, 119);
    CHECK(!s.c.on);
    ctl_run(&s, 1);                         /* minimum off time over */
    CHECK(s.c.on);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_DIFF);
    ctl_heat(&s, 599, 6);
    CHECK_EQ(s.a.active, 0);
    ctl_heat(&s, 1, 1);
    CHECK_EQ(s.t.t_col, 500);
    CHECK_EQ(s.a.active, E6);
    CHECK(s.c.on);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_DIFF);

    /* on to the collector protection: Hot next to E6 */
    while (s.t.t_col < 1100) {
        ctl_heat(&s, 6, 6);
    }
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_COL_PROTECT);
    CHECK_EQ(s.a.active, E6 | HOT);

    /* on to the boiling point: the pump stops, E5 next to E6, no Hot */
    while (s.t.t_col < 1270) {
        ctl_heat(&s, 6, 6);
    }
    CHECK(!s.c.on);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_COL_BOILING);
    CHECK_EQ(s.a.active, E5 | E6);

    /* The same with the pump test: no E6 during the test; after it the pump goes on by the
     * difference and E6 comes 10 min after the end of the test. */
    s = ctl_sim(400, 300);
    ctl_run(&s, 10);
    sh_pump_ctl_plus(&s.c, 300, s.now);
    ctl_step(&s);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_TEST);
    ctl_heat(&s, 299, 6);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_TEST);
    ctl_heat(&s, 1, 6);
    CHECK(s.c.on);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_DIFF);
    ctl_heat(&s, 599, 6);
    CHECK_EQ(s.a.active, 0);
    ctl_heat(&s, 1, 1);
    CHECK_EQ(s.a.active, E6);

    /* With flow: the tank follows, the difference stays low — no E6 all day. */
    s = ctl_sim(400, 300);
    for (int i = 0; i < 6 * 3600; i++) {
        if (i % 60 == 0 && s.c.on) {
            s.t.t_tank++;                   /* 1 K in 10 min */
            s.t.t_col = (int16_t)(s.t.t_tank + 80);
        }
        ctl_run(&s, 1);
        CHECK_EQ(s.a.active & E6, 0);
    }
    CHECK(s.c.on);

    /* The tank at its ceiling by the collector protection: E4, and the limits of the alarms are
     * those of the pump all the way (checked in every step). */
    s = ctl_sim(1120, 900);
    ctl_run(&s, 1);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_COL_PROTECT);
    CHECK_EQ(s.a.active, HOT);
    for (int16_t t = 900; t <= 960; t++) {
        s.t.t_tank = t;
        ctl_run(&s, 1);
    }
    CHECK_EQ(s.a.active, E4);
    CHECK(!s.c.on);
    for (int16_t t = 960; t >= 920; t--) {
        s.t.t_tank = t;
        ctl_run(&s, 1);
    }
    CHECK_EQ(s.a.active, HOT);              /* below 93 °C: the protection runs the pump again */
    for (int16_t t = 1120; t <= 1280; t++) {
        s.t.t_col = t;
        ctl_run(&s, 1);
    }
    CHECK_EQ(s.a.active, E5);
    for (int16_t t = 1280; t >= 1240; t--) {
        s.t.t_col = t;
        ctl_run(&s, 1);
    }
    CHECK_EQ(s.a.active, HOT);

    /* Tank sensor faulty, the collector at its emergency temperature with the pump stopped: the
     * probe runs the pump — Hot next to E2. */
    s = ctl_sim(500, 400);
    s.t.tank_fault = true;
    ctl_run(&s, 10);
    CHECK(!s.c.on);
    CHECK_EQ(s.a.active, E2);
    s.t.t_col = 1100;
    ctl_run(&s, 1);
    CHECK_EQ(s.c.reason, SH_PUMP_REASON_TF_PROBE);
    CHECK_EQ(s.a.active, E2 | HOT);
    ctl_run(&s, 180);                       /* still >= 93 °C: the probe failed */
    CHECK(!s.c.on);
    CHECK_EQ(s.a.active, E2);

    /* Collector sensor faulty: the probes are not judged for E6. */
    s = ctl_sim(800, 300);
    s.t.col_fault = true;
    ctl_run(&s, 24 * 3600);
    CHECK_EQ(s.a.active, E1);
}

int main(void)
{
    test_sensor_faults();
    test_e4_tank_ceiling();
    test_e5_col_boiling();
    test_hot();
    test_e6_declared();
    test_e6_run();
    test_e6_cleared();
    test_e6_params();
    test_texts();
    test_screen();
    test_with_pump_ctl();
    return test_summary("test_alarms");
}
