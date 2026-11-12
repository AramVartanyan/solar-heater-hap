/* Host test of the pump control above the normal mode (main/sh_pump_ctl.c): pump test, anti-seize,
 * sensor faults (doc/tasks/step2-settings-menu-pump-sensors.md 3.3 - 3.5). A simulation with a
 * 1 s clock, as the control task runs it. Temperatures are tenths of °C, times are seconds. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_pump_ctl.h"

/* Defaults of the task (Kconfig menu "Control") */
static const sh_pump_params_t P = {
    .diff_on = 60,
    .diff_off = 40,
    .min_run_s = 180,
    .min_off_s = 120,
    .tank_max = 700,
    .col_min = 250,
    .col_protect = 1100,
    .col_protect_cap = 1150,    /* water, 1.5 bar, collector maximum 120 °C */
    .col_boiling = 1270,
    .frost_on = false,          /* the frost protection has its own tests */
    .frost = 40,
};

#define H       3600u
#define INV     SH_TEMP_INVALID

typedef struct {
    sh_pump_ctl_t c;
    sh_pump_params_t p;
    sh_pump_temps_t t;
    uint32_t now;
    uint32_t test_s;
} sim_t;

/* Boot: pump off since 0 s. */
static sim_t sim(int16_t col, int16_t tank)
{
    sim_t s;
    memset(&s, 0, sizeof(s));
    s.p = P;
    s.t.t_col = col;
    s.t.t_tank = tank;
    s.test_s = 300;
    return s;
}

static void temps(sim_t *s, int16_t col, int16_t tank)
{
    s->t.t_col = col;
    s->t.t_tank = tank;
}

static void faults(sim_t *s, bool col, bool tank)
{
    s->t.col_fault = col;
    s->t.tank_fault = tank;
}

/* `n` steps of 1 s */
static void run(sim_t *s, uint32_t n)
{
    while (n--) {
        s->now++;
        sh_pump_ctl_step(&s->c, &s->p, &s->t, s->now);
    }
}

static void run_to(sim_t *s, uint32_t t)
{
    run(s, t - s->now);
}

/* Button commands: applied, then a step at the same second (as the control task does). */
static void plus(sim_t *s)
{
    sh_pump_ctl_plus(&s->c, s->test_s, s->now);
    sh_pump_ctl_step(&s->c, &s->p, &s->t, s->now);
}

static void minus(sim_t *s)
{
    sh_pump_ctl_minus(&s->c, s->now);
    sh_pump_ctl_step(&s->c, &s->p, &s->t, s->now);
}

static void stop(sim_t *s)
{
    sh_pump_ctl_stop(&s->c, s->now);
    sh_pump_ctl_step(&s->c, &s->p, &s->t, s->now);
}

static uint32_t left(const sim_t *s)
{
    return sh_pump_ctl_test_left(&s->c, s->now);
}

/* Events since the last call */
static uint8_t events(sim_t *s)
{
    uint8_t e = s->c.events;
    s->c.events = 0;
    return e;
}

static void expect(int line, const sim_t *s, bool on, sh_pump_reason_t reason)
{
    s_checks++;
    if (s->c.on != on || s->c.reason != reason) {
        s_failed++;
        printf("FAIL line %d, t = %u s: pump %s (%s), expected %s (%s)\n", line, (unsigned)s->now,
               s->c.on ? "ON" : "OFF", sh_pump_reason_name(s->c.reason), on ? "ON" : "OFF",
               sh_pump_reason_name(reason));
    }
}

#define ON(s, r)    expect(__LINE__, &(s), true, SH_PUMP_REASON_##r)
#define OFF(s, r)   expect(__LINE__, &(s), false, SH_PUMP_REASON_##r)

/* Pump off for a while, minimum off time over, sensors fine, no difference. */
static sim_t sim_idle(void)
{
    sim_t s = sim(300, 300);
    run(&s, 200);
    events(&s);
    return s;
}

/* ==== Pump test (3.3) ===================================================================== */

static void test_test_starts_at_once(void)
{
    sim_t s = sim_idle();
    OFF(s, DIFF);
    plus(&s);
    ON(s, TEST);
    CHECK(events(&s) & SH_PUMP_EVT_TEST_START);
    CHECK_EQ(left(&s), 300);

    /* right after a stop: no minimum off time */
    s = sim(500, 400);
    run(&s, 120);
    ON(s, DIFF);
    temps(&s, 430, 400);
    run(&s, 180);
    OFF(s, DIFF);
    run(&s, 1);
    plus(&s);
    ON(s, TEST);

    /* at boot */
    s = sim(300, 300);
    plus(&s);
    ON(s, TEST);
}

static void test_test_duration(void)
{
    sim_t s = sim_idle();           /* now = 200 */
    plus(&s);                       /* ends at 500 */
    run_to(&s, 499);
    ON(s, TEST);
    CHECK_EQ(left(&s), 1);
    CHECK(!(events(&s) & SH_PUMP_EVT_TEST_END));
    run(&s, 1);
    OFF(s, DIFF);
    CHECK(events(&s) & SH_PUMP_EVT_TEST_END);
    CHECK(!s.c.test);
    CHECK_EQ(left(&s), 0);

    s = sim_idle();
    s.test_s = 900;
    plus(&s);
    run(&s, 899);
    ON(s, TEST);
    run(&s, 1);
    OFF(s, DIFF);
}

static void test_test_plus(void)
{
    sim_t s = sim_idle();
    plus(&s);
    plus(&s);                       /* +60 s */
    CHECK_EQ(left(&s), 360);
    run(&s, 100);
    plus(&s);
    CHECK_EQ(left(&s), 320);
    for (int i = 0; i < 12; i++) {
        plus(&s);
    }
    CHECK_EQ(left(&s), 900);        /* at most 900 s left */
    ON(s, TEST);
    run(&s, 59);                    /* 841 s left: +60 s would be 901 s */
    plus(&s);
    CHECK_EQ(left(&s), 900);
    CHECK(!(events(&s) & SH_PUMP_EVT_TEST_END));    /* "+" never restarts the test */
}

static void test_test_minus(void)
{
    sim_t s = sim_idle();
    minus(&s);                      /* no test: nothing */
    OFF(s, DIFF);
    CHECK(!s.c.test);

    plus(&s);
    minus(&s);
    CHECK_EQ(left(&s), 240);
    run(&s, 179);                   /* 61 s left */
    minus(&s);
    CHECK_EQ(left(&s), 1);          /* over 60 s: -60 s */
    minus(&s);
    CHECK_EQ(left(&s), 1);          /* not over 60 s: no change */
    ON(s, TEST);

    s = sim_idle();
    plus(&s);
    run(&s, 240);                   /* exactly 60 s left */
    minus(&s);
    CHECK_EQ(left(&s), 60);

    s = sim_idle();                 /* never less than 1 min after the start */
    s.test_s = 90;
    plus(&s);
    run(&s, 5);                     /* 85 s left: -60 s would end it 30 s after the start */
    minus(&s);
    CHECK_EQ(left(&s), 55);
    run(&s, 54);
    ON(s, TEST);
    run(&s, 1);
    OFF(s, DIFF);
}

static void test_test_stop(void)
{
    sim_t s = sim_idle();           /* 200 */
    plus(&s);
    run(&s, 30);
    stop(&s);                       /* within the first minute: ends 60 s after the start */
    ON(s, TEST);
    run_to(&s, 259);
    ON(s, TEST);
    run(&s, 1);
    OFF(s, DIFF);
    CHECK(events(&s) & SH_PUMP_EVT_TEST_END);

    s = sim_idle();
    plus(&s);
    run(&s, 100);
    stop(&s);                       /* at once */
    OFF(s, DIFF);
    CHECK(!s.c.test);

    s = sim_idle();
    plus(&s);
    run(&s, 60);                    /* exactly 1 min */
    stop(&s);
    OFF(s, DIFF);
}

static void test_test_emergency_only(void)
{
    sim_t s = sim_idle();
    plus(&s);
    temps(&s, 1269, 300);
    run(&s, 1);
    ON(s, TEST);
    events(&s);
    temps(&s, 1270, 300);           /* boiling point: off at once, the test ends */
    run(&s, 1);
    OFF(s, COL_BOILING);
    CHECK(!s.c.test);
    CHECK_EQ(events(&s), SH_PUMP_EVT_TEST_END);
    temps(&s, 1249, 300);           /* released: no test any more, the normal mode decides */
    run(&s, 1);
    ON(s, COL_PROTECT);
    CHECK(!s.c.test);

    s = sim_idle();
    plus(&s);
    temps(&s, 300, 949);
    run(&s, 1);
    ON(s, TEST);
    temps(&s, 300, 950);            /* tank ceiling: off at once, the test ends */
    run(&s, 1);
    OFF(s, TANK_CEILING);
    CHECK(!s.c.test);
    temps(&s, 300, 929);
    run(&s, 1);
    OFF(s, TANK_MAX);
    CHECK(!s.c.test);

    /* nothing else stops it */
    s = sim_idle();
    plus(&s);
    temps(&s, 900, 800);            /* above Tmax */
    run(&s, 1);
    ON(s, TEST);
    temps(&s, 200, 400);            /* collector colder than the tank */
    run(&s, 1);
    ON(s, TEST);
    temps(&s, 1100, 400);           /* collector protection */
    run(&s, 1);
    ON(s, TEST);
    CHECK(s.c.test);
}

static void test_test_refused_by_limits(void)
{
    sim_t s = sim(1270, 300);       /* boiling point */
    run_to(&s, 200);
    OFF(s, COL_BOILING);
    const uint32_t switched = s.c.switched_s;
    events(&s);
    plus(&s);                       /* started and ended in the same step: refused */
    OFF(s, COL_BOILING);
    CHECK(!s.c.test);
    CHECK_EQ(events(&s), SH_PUMP_EVT_TEST_START | SH_PUMP_EVT_TEST_END);
    CHECK_EQ(left(&s), 0);
    CHECK_EQ(s.c.switched_s, switched);     /* the pump did not run: the 24 h are not restarted */

    s = sim(300, 950);              /* tank ceiling */
    run_to(&s, 200);
    plus(&s);
    OFF(s, TANK_CEILING);
    CHECK(!s.c.test);
}

static void test_test_with_faulty_sensors(void)
{
    sim_t s = sim_idle();
    plus(&s);
    faults(&s, false, true);
    temps(&s, 300, 960);            /* a faulty tank sensor: the ceiling cannot be checked */
    run(&s, 1);
    ON(s, TEST);
    faults(&s, true, false);
    temps(&s, 1300, 300);           /* a faulty collector sensor: the boiling point neither */
    run(&s, 1);
    ON(s, TEST);
    faults(&s, true, true);
    run(&s, 1);
    ON(s, TEST);

    s = sim(300, 300);              /* both faulty: the pump is off, the test is allowed */
    faults(&s, true, true);
    run(&s, 10);
    OFF(s, BOTH_FAULT);
    plus(&s);
    ON(s, TEST);
    run(&s, 300);
    OFF(s, BOTH_FAULT);
}

static void test_test_end_continues_by_difference(void)
{
    sim_t s = sim_idle();           /* 200 */
    s.test_s = 60;
    plus(&s);
    temps(&s, 500, 400);
    run_to(&s, 260);                /* end: the difference would start a stopped pump */
    ON(s, DIFF);
    CHECK_EQ(s.c.switched_s, 260);  /* no switch of the relay; it runs by the difference from now */
    temps(&s, 420, 400);
    run(&s, 1);
    ON(s, MIN_RUN);                 /* the minimum run time counts from the end of the test */
    run_to(&s, 439);
    ON(s, MIN_RUN);
    run(&s, 1);
    OFF(s, DIFF);
}

static void test_test_end_stops(void)
{
    sim_t s = sim_idle();
    s.test_s = 60;
    plus(&s);
    run_to(&s, 260);
    OFF(s, DIFF);
    temps(&s, 500, 400);            /* the minimum off time counts from the end of the test */
    run(&s, 1);
    OFF(s, MIN_OFF);
    run_to(&s, 379);
    OFF(s, MIN_OFF);
    run(&s, 1);
    ON(s, DIFF);

    s = sim_idle();
    s.test_s = 60;
    plus(&s);
    temps(&s, 900, 800);            /* Tmax */
    run_to(&s, 260);
    OFF(s, TANK_MAX);

    s = sim_idle();
    s.test_s = 60;
    plus(&s);
    temps(&s, 1100, 800);           /* collector protection */
    run_to(&s, 260);
    ON(s, COL_PROTECT);

    s = sim(1100, 800);             /* a test during the collector protection, ending with the */
    run(&s, 200);                   /* collector inside its return zone: no start there */
    ON(s, COL_PROTECT);
    s.test_s = 60;
    plus(&s);
    ON(s, TEST);
    temps(&s, 1090, 800);
    run(&s, 60);
    OFF(s, TANK_MAX);
}

static void test_test_end_in_fault_modes(void)
{
    sim_t s = sim_idle();
    faults(&s, false, true);
    temps(&s, 650, 300);
    s.test_s = 60;
    plus(&s);
    run(&s, 60);
    ON(s, TF_RUN);

    s = sim_idle();
    faults(&s, false, true);
    temps(&s, 500, 300);
    s.test_s = 60;
    plus(&s);
    run(&s, 60);
    OFF(s, TF_COLD);

    s = sim_idle();
    faults(&s, true, false);
    run(&s, 1);
    ON(s, CF_PROBE);
    s.test_s = 60;
    plus(&s);                       /* replaces the probe */
    ON(s, TEST);
    run(&s, 60);
    ON(s, CF_PROBE);                /* a probe is due: the pump keeps running */

    s = sim_idle();
    faults(&s, true, false);
    run(&s, 601);                   /* the probe fails: next one in 30 min */
    OFF(s, CF_WAIT);
    s.test_s = 60;
    plus(&s);
    run(&s, 60);
    OFF(s, CF_WAIT);
}

/* ==== Anti-seize (3.4) ==================================================================== */

static void test_seize_after_24h(void)
{
    sim_t s = sim(300, 300);        /* collector as warm as the tank, off since boot */
    run_to(&s, 24 * H - 1);
    OFF(s, DIFF);
    CHECK(!(events(&s) & SH_PUMP_EVT_SEIZE_START));
    run(&s, 1);
    ON(s, ANTI_SEIZE);
    CHECK(events(&s) & SH_PUMP_EVT_SEIZE_START);
    run(&s, 59);
    ON(s, ANTI_SEIZE);
    run(&s, 1);                     /* 60 s */
    OFF(s, DIFF);
    CHECK(events(&s) & SH_PUMP_EVT_SEIZE_END);

    const uint32_t stopped = s.now;
    run_to(&s, stopped + 24 * H - 1);   /* the next one 24 h after this run */
    OFF(s, DIFF);
    run(&s, 1);
    ON(s, ANTI_SEIZE);
}

static void test_seize_waits_for_warm_collector(void)
{
    sim_t s = sim(299, 300);        /* collector 0.1 K colder */
    run_to(&s, 24 * H);
    OFF(s, DIFF);
    run_to(&s, 30 * H);
    OFF(s, DIFF);
    temps(&s, 300, 300);
    run(&s, 1);
    ON(s, ANTI_SEIZE);

    s = sim(200, 300);              /* the moment does not come: 12 h later anyway */
    run_to(&s, 36 * H - 1);
    OFF(s, DIFF);
    run(&s, 1);
    ON(s, ANTI_SEIZE);
}

static void test_seize_counts_from_last_run(void)
{
    sim_t s = sim(300, 300);
    run_to(&s, 20 * H);
    temps(&s, 500, 400);
    run(&s, 1);
    ON(s, DIFF);
    temps(&s, 300, 300);
    run(&s, 180);
    OFF(s, DIFF);
    const uint32_t stopped = s.now;
    run_to(&s, 24 * H + 60);        /* 24 h after boot: no */
    OFF(s, DIFF);
    run_to(&s, stopped + 24 * H - 1);
    OFF(s, DIFF);
    run(&s, 1);
    ON(s, ANTI_SEIZE);

    s = sim(300, 300);              /* a pump test counts as running */
    run_to(&s, 10 * H);
    plus(&s);
    run(&s, 300);
    const uint32_t test_end = s.now;
    run_to(&s, test_end + 24 * H - 1);
    OFF(s, DIFF);
    run(&s, 1);
    ON(s, ANTI_SEIZE);
}

static void test_seize_end_continues(void)
{
    sim_t s = sim(300, 300);
    run_to(&s, 24 * H);
    ON(s, ANTI_SEIZE);
    temps(&s, 500, 400);
    run(&s, 60);
    ON(s, DIFF);                    /* like a stopped pump with the minimum off time over */
    CHECK_EQ(s.c.switched_s, 24 * H + 60);  /* the minimum run time counts from the end */
}

static void test_seize_emergency_only(void)
{
    sim_t s = sim(960, 950);        /* tank at the ceiling */
    run_to(&s, 37 * H);
    OFF(s, TANK_CEILING);
    temps(&s, 940, 929);            /* released: collector >= tank */
    run(&s, 1);
    ON(s, ANTI_SEIZE);
    temps(&s, 940, 950);            /* the ceiling stops it */
    run(&s, 1);
    OFF(s, TANK_CEILING);
    run(&s, 60);
    OFF(s, TANK_CEILING);
    CHECK(!s.c.seize);

    s = sim(1270, 300);             /* boiling point */
    run_to(&s, 37 * H);
    OFF(s, COL_BOILING);
    temps(&s, 1249, 300);
    run(&s, 1);
    ON(s, COL_PROTECT);             /* the normal mode starts it first */

    s = sim(300, 300);              /* nothing else stops it: Tmax */
    run_to(&s, 24 * H);
    ON(s, ANTI_SEIZE);
    temps(&s, 800, 750);
    run(&s, 1);
    ON(s, ANTI_SEIZE);
}

static void test_seize_with_faulty_sensors(void)
{
    sim_t s = sim(300, 300);
    faults(&s, true, true);         /* both faulty: the pump stays off, no anti-seize either */
    run_to(&s, 40 * H);
    OFF(s, BOTH_FAULT);
    CHECK_EQ(s.c.switched_s, 0);    /* never switched on */

    s = sim(300, 200);              /* tank faulty: "collector >= tank" cannot be judged */
    faults(&s, false, true);
    run_to(&s, 36 * H - 1);
    OFF(s, TF_COLD);
    run(&s, 1);
    ON(s, ANTI_SEIZE);
    run(&s, 60);
    OFF(s, TF_COLD);
}

/* ==== Tank sensor faulty (3.5) ============================================================= */

static void test_tf_start_stop(void)
{
    sim_t s = sim(599, 400);
    faults(&s, false, true);
    run(&s, 200);
    OFF(s, TF_COLD);
    temps(&s, 600, 400);            /* 60.0 °C */
    run(&s, 1);
    ON(s, TF_RUN);
    temps(&s, 579, 400);            /* within the minimum run time */
    run(&s, 1);
    ON(s, MIN_RUN);
    temps(&s, 580, 400);            /* 58.0 °C: not below */
    run(&s, 300);
    ON(s, TF_RUN);
    temps(&s, 579, 400);
    run(&s, 1);
    OFF(s, TF_COLD);
    temps(&s, 700, 400);            /* the minimum off time applies */
    run(&s, 119);
    OFF(s, MIN_OFF);
    run(&s, 1);
    ON(s, TF_RUN);
}

static void test_tf_hot(void)
{
    sim_t s = sim(700, 400);
    faults(&s, false, true);
    run(&s, 200);
    ON(s, TF_RUN);
    run(&s, 1);
    temps(&s, 949, 400);
    run(&s, 1);
    ON(s, TF_RUN);
    temps(&s, 950, 400);            /* 95.0 °C: at once */
    run(&s, 1);
    OFF(s, TF_HOT);
    temps(&s, 930, 400);
    run(&s, 300);
    OFF(s, TF_HOT);
    temps(&s, 929, 400);            /* below 93 °C, the minimum off time is over */
    run(&s, 1);
    ON(s, TF_RUN);

    s = sim(700, 400);              /* within the minimum run time too */
    faults(&s, false, true);
    run(&s, 200);
    temps(&s, 950, 400);
    run(&s, 1);
    OFF(s, TF_HOT);
    temps(&s, 929, 400);
    run(&s, 1);
    OFF(s, MIN_OFF);
}

static void test_tf_ignores_tank(void)
{
    sim_t s = sim(700, 900);        /* last tank value above Tmax, even above the ceiling */
    faults(&s, false, true);
    run(&s, 200);
    ON(s, TF_RUN);
    temps(&s, 700, 960);
    run(&s, 1);
    ON(s, TF_RUN);
}

static void test_tf_boiling(void)
{
    sim_t s = sim(1000, 400);
    faults(&s, false, true);
    run(&s, 200);
    OFF(s, TF_HOT);
    temps(&s, 1100, 400);
    run(&s, 1);
    ON(s, TF_PROBE);
    temps(&s, 1269, 400);
    run(&s, 10);
    ON(s, TF_PROBE);
    temps(&s, 1270, 400);           /* boiling point: stops the probe at once */
    run(&s, 1);
    OFF(s, COL_BOILING);
    const uint32_t stopped = s.now;
    temps(&s, 1249, 400);           /* the probe failed: next one in 5 h */
    run(&s, 1);
    OFF(s, TF_HOT);
    run_to(&s, stopped + 5 * H - 1);
    OFF(s, TF_HOT);
    run(&s, 1);
    ON(s, TF_PROBE);

    s = sim(700, 400);              /* also by the normal rules */
    faults(&s, false, true);
    run(&s, 200);
    ON(s, TF_RUN);
    temps(&s, 1270, 400);
    run(&s, 1);
    OFF(s, COL_BOILING);
}

static void test_tf_stop_after_min_run(void)
{
    sim_t s = sim(600, 400);
    faults(&s, false, true);
    run(&s, 200);
    ON(s, TF_RUN);
    const uint32_t on_at = s.c.switched_s;
    temps(&s, 579, 400);            /* below 58 °C: stops only after the minimum run time */
    run_to(&s, on_at + 179);
    ON(s, MIN_RUN);
    run(&s, 1);
    OFF(s, TF_COLD);
}

static void test_tf_probe(void)
{
    sim_t s = sim(1000, 400);       /* collector hot, pump stopped */
    faults(&s, false, true);
    run(&s, 200);
    OFF(s, TF_HOT);
    temps(&s, 1099, 400);
    run(&s, 1);
    OFF(s, TF_HOT);
    temps(&s, 1100, 400);           /* the collector protection threshold */
    run(&s, 1);
    ON(s, TF_PROBE);
    const uint32_t t0 = s.now;
    temps(&s, 900, 400);
    run_to(&s, t0 + 179);
    ON(s, TF_PROBE);                /* the minimum run time, whatever the collector does */
    temps(&s, 929, 400);
    run(&s, 1);
    ON(s, TF_RUN);                  /* below 93 °C: the tank takes the heat */
    temps(&s, 580, 400);
    run(&s, 1);
    ON(s, TF_RUN);
    temps(&s, 950, 400);
    run(&s, 1);
    OFF(s, TF_HOT);

    s = sim(1000, 400);
    faults(&s, false, true);
    run(&s, 200);
    temps(&s, 1100, 400);
    run(&s, 1);
    const uint32_t t1 = s.now;
    temps(&s, 930, 400);
    run_to(&s, t1 + 180);           /* still >= 93 °C: tank full */
    OFF(s, TF_HOT);
    temps(&s, 1150, 400);
    run_to(&s, t1 + 180 + 5 * H - 1);
    OFF(s, TF_HOT);
    run(&s, 1);
    ON(s, TF_PROBE);                /* 5 h later */
}

static void test_tf_probe_ignores_min_off(void)
{
    sim_t s = sim(900, 400);
    faults(&s, false, true);
    run(&s, 200);
    ON(s, TF_RUN);
    temps(&s, 950, 400);
    run(&s, 1);
    OFF(s, TF_HOT);
    temps(&s, 1100, 400);           /* 1 s after the stop */
    run(&s, 1);
    ON(s, TF_PROBE);
}

static void test_tf_probe_threshold_below_hot(void)
{
    sim_t s = sim(850, 400);
    s.p.col_protect = 900;          /* below 95 °C */
    faults(&s, false, true);
    run(&s, 200);
    ON(s, TF_RUN);
    const uint32_t on_at = s.c.switched_s;
    temps(&s, 899, 400);
    run(&s, 1);
    ON(s, TF_RUN);                  /* a running pump is no probe ... */
    temps(&s, 950, 400);
    run(&s, 1);
    ON(s, TF_PROBE);                /* ... until 95 °C would stop it: no off and on again */
    CHECK_EQ(s.c.switched_s, on_at);
    run(&s, 179);
    ON(s, TF_PROBE);
    run(&s, 1);
    OFF(s, TF_HOT);                 /* >= 93 °C after the minimum run time */

    s = sim(850, 400);              /* a stopped pump: probe at the threshold */
    s.p.col_protect = 900;
    faults(&s, false, true);
    run(&s, 200);
    temps(&s, 500, 400);
    run(&s, 200);
    OFF(s, TF_COLD);
    temps(&s, 900, 400);
    run(&s, 1);
    ON(s, TF_PROBE);
}

static void test_tf_enter_and_recover(void)
{
    sim_t s = sim(700, 600);
    run(&s, 200);
    ON(s, DIFF);
    const uint32_t on_at = s.c.switched_s;
    faults(&s, false, true);        /* the running pump goes on by the new rules */
    run(&s, 1);
    ON(s, TF_RUN);
    CHECK_EQ(s.c.switched_s, on_at);
    run(&s, 300);
    faults(&s, false, false);       /* valid again, difference 1 K, run time over */
    temps(&s, 700, 690);
    run(&s, 1);
    OFF(s, DIFF);

    s = sim(300, 200);              /* recovery within the minimum run time */
    faults(&s, false, true);
    run(&s, 200);
    OFF(s, TF_COLD);
    temps(&s, 700, 200);
    run(&s, 1);
    ON(s, TF_RUN);
    run(&s, 10);
    faults(&s, false, false);
    temps(&s, 700, 690);
    run(&s, 1);
    ON(s, MIN_RUN);

    s = sim(700, 200);              /* recovery of a stopped pump: the minimum off time applies */
    faults(&s, false, true);
    run(&s, 200);
    temps(&s, 500, 200);
    run(&s, 200);
    OFF(s, TF_COLD);
    faults(&s, false, false);
    temps(&s, 500, 400);
    run(&s, 1);
    OFF(s, MIN_OFF);
}

static void test_tf_no_collector_value(void)
{
    sim_t s = sim(INV, 400);
    faults(&s, false, true);
    run(&s, 200);
    OFF(s, SENSOR);
}

/* ==== Collector sensor faulty (3.5) ======================================================== */

static void test_cf_first_probe(void)
{
    sim_t s = sim_idle();
    faults(&s, true, false);
    run(&s, 1);
    ON(s, CF_PROBE);                /* at once */

    s = sim(500, 400);
    run(&s, 120);
    ON(s, DIFF);
    temps(&s, 400, 400);
    run(&s, 180);
    OFF(s, DIFF);
    const uint32_t stopped = s.now;
    faults(&s, true, false);
    run(&s, 1);
    OFF(s, MIN_OFF);                /* the minimum off time applies */
    run_to(&s, stopped + 119);
    OFF(s, MIN_OFF);
    run(&s, 1);
    ON(s, CF_PROBE);
}

/* A collector fault with the pump off for long; returns the start of the first probe. */
static uint32_t cf_probe_started(sim_t *s, int16_t tank)
{
    *s = sim(300, tank);
    run(s, 200);
    faults(s, true, false);
    run(s, 1);
    return s->now;
}

static void test_cf_probe_judged(void)
{
    sim_t s;
    uint32_t t0 = cf_probe_started(&s, 400);
    ON(s, CF_PROBE);
    temps(&s, 300, 405);
    run_to(&s, t0 + 599);
    ON(s, CF_PROBE);
    run(&s, 1);                     /* 10 min, +0.5 K */
    ON(s, CF_RUN);

    t0 = cf_probe_started(&s, 400);
    temps(&s, 300, 404);            /* +0.4 K */
    run_to(&s, t0 + 600);
    OFF(s, CF_WAIT);
    run_to(&s, t0 + 600 + 1799);
    OFF(s, CF_WAIT);
    run(&s, 1);                     /* 30 min later */
    ON(s, CF_PROBE);
}

static void test_cf_run_checks(void)
{
    sim_t s;
    const uint32_t t0 = cf_probe_started(&s, 400);
    temps(&s, 300, 405);
    run_to(&s, t0 + 600);
    ON(s, CF_RUN);
    temps(&s, 300, 410);            /* +0.5 K in the next 10 min */
    run_to(&s, t0 + 1200);
    ON(s, CF_RUN);
    temps(&s, 300, 414);            /* +0.4 K */
    run_to(&s, t0 + 1799);
    ON(s, CF_RUN);
    run(&s, 1);
    OFF(s, CF_WAIT);
    run_to(&s, t0 + 1800 + 1799);   /* after a run with a rise: 30 min */
    OFF(s, CF_WAIT);
    run(&s, 1);
    ON(s, CF_PROBE);
}

static void test_cf_wait_doubles(void)
{
    sim_t s;
    uint32_t t = cf_probe_started(&s, 400);
    const uint32_t waits[] = { 1800, 3600, 7200, 7200 };
    for (int i = 0; i < 4; i++) {
        run_to(&s, t + 600);        /* failed probe */
        OFF(s, CF_WAIT);
        CHECK_EQ(s.c.cf_fails, i < 3 ? i + 1 : 3);
        run_to(&s, t + 600 + waits[i] - 1);
        OFF(s, CF_WAIT);
        run(&s, 1);
        ON(s, CF_PROBE);
        t = s.now;
    }
    temps(&s, 300, 405);            /* a probe with a rise ... */
    run_to(&s, t + 600);
    ON(s, CF_RUN);
    CHECK_EQ(s.c.cf_fails, 0);
    run_to(&s, t + 1200);           /* ... no rise in the next 10 min */
    OFF(s, CF_WAIT);
    run_to(&s, t + 1200 + 1799);    /* back to 30 min */
    OFF(s, CF_WAIT);
    run(&s, 1);
    ON(s, CF_PROBE);
    run(&s, 600);                   /* failed again: 30 min (the first failure in a row) */
    OFF(s, CF_WAIT);
    CHECK_EQ(s.c.cf_next_s, s.now + 1800);
}

static void test_cf_tank_ceiling(void)
{
    sim_t s;
    cf_probe_started(&s, 900);
    temps(&s, 300, 949);
    run(&s, 100);
    ON(s, CF_PROBE);
    temps(&s, 300, 950);            /* at once */
    run(&s, 1);
    OFF(s, TANK_CEILING);
    const uint32_t stopped = s.now;
    temps(&s, 300, 929);            /* released, but no probe for 5 h */
    run(&s, 1);
    OFF(s, CF_WAIT);
    run_to(&s, stopped + 5 * H - 1);
    OFF(s, CF_WAIT);
    run(&s, 1);
    ON(s, CF_PROBE);

    cf_probe_started(&s, 800);      /* Tmax is not used */
    run(&s, 300);
    ON(s, CF_PROBE);
}

static void test_cf_tank_ceiling_pump_off(void)
{
    /* the tank reaches the ceiling with the pump off (heater): no probe for 5 h either */
    sim_t s = sim(300, 900);
    faults(&s, true, false);
    run_to(&s, 2400);               /* first probe 120 - 720 s failed, the next one at 2520 s */
    OFF(s, CF_WAIT);
    temps(&s, 300, 950);
    run(&s, 1);
    OFF(s, TANK_CEILING);
    const uint32_t reached = s.now;
    CHECK_EQ(s.c.cf_next_s, reached + 5 * H);
    run(&s, 600);                   /* the same episode does not move the 5 h */
    CHECK_EQ(s.c.cf_next_s, reached + 5 * H);
    temps(&s, 300, 929);
    run(&s, 1);
    OFF(s, CF_WAIT);
    run_to(&s, reached + 5 * H - 1);
    OFF(s, CF_WAIT);
    run(&s, 1);
    ON(s, CF_PROBE);
}

static void test_cf_heater(void)
{
    /* the tank rises 0.5 K in 10 min with the pump off: heater, the probe waits */
    sim_t s = sim(300, 400);
    run(&s, 200);
    int16_t tank = 400;
    for (int i = 0; i < 10; i++) {  /* +0.1 K every 2 min, 20 min */
        temps(&s, 300, ++tank);
        run(&s, 120);
    }
    CHECK(sh_pump_ctl_heater(&s.c));
    faults(&s, true, false);
    run(&s, 1);
    OFF(s, CF_HEATER);
    for (int i = 0; i < 5; i++) {   /* the heater goes on */
        temps(&s, 300, ++tank);
        run(&s, 120);
        OFF(s, CF_HEATER);
    }
    const uint32_t stopped = s.now; /* it stops: the probe within 11 min */
    uint32_t started = 0;
    for (uint32_t i = 0; i < 700 && !started; i++) {
        run(&s, 1);
        if (s.c.on) {
            started = s.now;
        }
    }
    ON(s, CF_PROBE);
    CHECK(started > stopped && started <= stopped + 660);

    /* +0.4 K in 10 min: no heater */
    s = sim(300, 400);
    run(&s, 200);
    tank = 400;
    for (int i = 0; i < 8; i++) {   /* +0.1 K every 150 s */
        temps(&s, 300, ++tank);
        run(&s, 150);
    }
    CHECK(!sh_pump_ctl_heater(&s.c));
    faults(&s, true, false);
    run(&s, 1);
    ON(s, CF_PROBE);
}

static void test_cf_heater_needs_10_min_off(void)
{
    sim_t s = sim(500, 400);
    run(&s, 120);
    ON(s, DIFF);
    temps(&s, 400, 400);
    run(&s, 180);
    OFF(s, DIFF);
    const uint32_t stopped = s.now;
    run(&s, 1);                     /* the first sample after the stop: 40.0 °C */
    temps(&s, 400, 410);            /* +1 K: not judged before 10 min of samples */
    run_to(&s, stopped + 600);
    CHECK(!sh_pump_ctl_heater(&s.c));
    run(&s, 1);
    CHECK(sh_pump_ctl_heater(&s.c));
}

static void test_cf_enter_and_recover(void)
{
    sim_t s = sim(700, 600);
    run(&s, 200);
    ON(s, DIFF);
    const uint32_t on_at = s.c.switched_s;
    faults(&s, true, false);        /* the running pump becomes a probe */
    run(&s, 1);
    ON(s, CF_PROBE);
    CHECK_EQ(s.c.switched_s, on_at);
    const uint32_t t0 = s.now;
    temps(&s, 700, 605);
    run_to(&s, t0 + 600);
    ON(s, CF_RUN);
    faults(&s, false, false);       /* valid again: difference 5 K keeps it running */
    temps(&s, 700, 650);
    run(&s, 1);
    ON(s, DIFF);
    CHECK_EQ(s.c.switched_s, on_at);
}

static void test_cf_no_tank_value(void)
{
    sim_t s = sim(400, INV);
    faults(&s, true, false);
    run(&s, 200);
    OFF(s, SENSOR);
}

/* ==== Both faulty, no value yet, settings ================================================== */

static void test_both_faulty(void)
{
    sim_t s = sim(500, 400);
    run(&s, 200);
    ON(s, DIFF);
    faults(&s, true, true);
    run(&s, 1);                     /* at once */
    OFF(s, BOTH_FAULT);
}

static void test_no_value_until_fault(void)
{
    sim_t s = sim(INV, 400);        /* no collector value since boot, not yet faulty */
    run(&s, 200);
    OFF(s, SENSOR);
    faults(&s, true, false);        /* declared faulty: the probes start */
    run(&s, 1);
    ON(s, CF_PROBE);
}

/* ---- Frost protection (the rule itself: test_pump_logic) -------------------------------- */

static void test_frost_normal_mode_only(void)
{
    sim_t s = sim(20, 100);         /* collector 2.0 °C, below the 4.0 °C threshold */
    s.p.frost_on = true;
    run(&s, 1);
    ON(s, FROST);                   /* at once, 1 s after boot: no minimum off time */

    /* a faulty sensor: the frost protection is not checked */
    faults(&s, false, true);        /* tank */
    run(&s, 1);
    ON(s, MIN_RUN);                 /* the tank-fault mode takes the running pump over */
    run(&s, 200);
    OFF(s, TF_COLD);
    faults(&s, true, false);        /* collector: only its probes */
    run(&s, 1);
    CHECK(s.c.reason != SH_PUMP_REASON_FROST);
    faults(&s, true, true);
    run(&s, 1);
    OFF(s, BOTH_FAULT);

    /* both back: checked again; it starts at once, whatever the minimum off time */
    faults(&s, false, false);
    run(&s, 1);
    ON(s, FROST);
}

static void test_frost_with_test_and_seize(void)
{
    /* the pump test runs as before, the frost protection goes on after it */
    sim_t s = sim(20, 100);
    s.p.frost_on = true;
    run(&s, 10);
    ON(s, FROST);
    plus(&s);
    ON(s, TEST);
    run(&s, 300);
    ON(s, FROST);                   /* the test ended, the collector is still cold */
    CHECK(events(&s) & SH_PUMP_EVT_TEST_END);
    temps(&s, 60, 100);             /* threshold + 2 K; the minimum run time counts from the test end */
    run(&s, 179);
    ON(s, FROST);
    run(&s, 1);
    OFF(s, DIFF);

    /* tank below 5 °C: no frost protection, the anti-seize run still comes after 24 h */
    s = sim(20, 40);
    s.p.frost_on = true;
    run(&s, 100);
    OFF(s, DIFF);
    run_to(&s, 36 * H);
    ON(s, ANTI_SEIZE);
}

static void test_settings_apply_at_once(void)
{
    sim_t s = sim(800, 650);
    run(&s, 200);
    ON(s, DIFF);
    s.p.tank_max = 650;             /* tbo changed in the menu */
    run(&s, 1);
    OFF(s, TANK_MAX);
    s.p.tank_max = 700;
    s.p.diff_on = 200;              /* d.on 20 K: 15 K is not enough any more */
    run(&s, 200);
    OFF(s, DIFF);
}

int main(void)
{
    test_test_starts_at_once();
    test_test_duration();
    test_test_plus();
    test_test_minus();
    test_test_stop();
    test_test_emergency_only();
    test_test_refused_by_limits();
    test_test_with_faulty_sensors();
    test_test_end_continues_by_difference();
    test_test_end_stops();
    test_test_end_in_fault_modes();

    test_seize_after_24h();
    test_seize_waits_for_warm_collector();
    test_seize_counts_from_last_run();
    test_seize_end_continues();
    test_seize_emergency_only();
    test_seize_with_faulty_sensors();

    test_tf_start_stop();
    test_tf_hot();
    test_tf_ignores_tank();
    test_tf_boiling();
    test_tf_stop_after_min_run();
    test_tf_probe();
    test_tf_probe_ignores_min_off();
    test_tf_probe_threshold_below_hot();
    test_tf_enter_and_recover();
    test_tf_no_collector_value();

    test_cf_first_probe();
    test_cf_probe_judged();
    test_cf_run_checks();
    test_cf_wait_doubles();
    test_cf_tank_ceiling();
    test_cf_tank_ceiling_pump_off();
    test_cf_heater();
    test_cf_heater_needs_10_min_off();
    test_cf_enter_and_recover();
    test_cf_no_tank_value();

    test_both_faulty();
    test_no_value_until_fault();
    test_frost_normal_mode_only();
    test_frost_with_test_and_seize();
    test_settings_apply_at_once();

    return test_summary("test_pump_ctl");
}
