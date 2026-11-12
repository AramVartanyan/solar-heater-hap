/* Host test of the pump decision (main/sh_pump_logic.c) — no ESP-IDF needed.
 *
 *   make -C project/solar-heater-hap/test
 *
 * Every rule has its own test: exactly at the threshold, 0.1 K and 1 s on both sides, the minimum
 * times and the priorities. Temperatures are tenths of °C, times are seconds.
 *   - normal mode (doc/operating-modes.md, 2.1; doc/tasks/step1-control-loop.md, 3.2)
 *   - pump test, anti-seize, faulty sensors (doc/tasks/step2-settings-menu-pump-sensors.md,
 *     3.3 - 3.5) */

#include <stdio.h>
#include <string.h>
#include "sh_state.h"
#include "sh_pump_logic.h"
#include "test_util.h"

/* Defaults of the task (Kconfig menu "Control") */
static const sh_pump_params_t P = {
    .diff_on = 60,
    .diff_off = 40,
    .min_run_s = 180,
    .min_off_s = 120,
    .tank_max = 700,
    .col_min = 250,
    .col_protect = 1100,
    .test_s = 300,
};

#define LONG_AGO    10000u      /* s: every minimum time is over (and far from the 24 h of the anti-seize) */
#define HOUR        3600u

/* The pump with its clock and the state of the sensors, as the control task sees them. */
typedef struct {
    const sh_pump_params_t *p;
    sh_pump_state_t st;
    uint32_t now_s;             /* monotonic clock; the pump state starts at 0 */
    bool col_fault;             /* sensor declared faulty: its temperature is the old one */
    bool tank_fault;
    int16_t t_col;              /* the temperatures of the last step */
    int16_t t_tank;
} sim_t;

/* Pump off, nothing latched, minimum off time over. */
static sim_t sim_off(void)
{
    sim_t s = { .p = &P, .now_s = LONG_AGO };
    return s;
}

/* `dt_s` seconds later the temperatures are `t_col` / `t_tank`: one decision. */
static void step(sim_t *s, uint32_t dt_s, int16_t t_col, int16_t t_tank)
{
    const sh_pump_input_t in = { t_col, t_tank, s->col_fault, s->tank_fault };
    s->now_s += dt_s;
    s->t_col = t_col;
    s->t_tank = t_tank;
    sh_pump_decide(s->p, &s->st, &in, s->now_s);
}

/* One decision every second for `dt_s` seconds, as the control task does it. */
static void run(sim_t *s, uint32_t dt_s, int16_t t_col, int16_t t_tank)
{
    for (uint32_t i = 0; i < dt_s; i++) {
        step(s, 1, t_col, t_tank);
    }
}

static void expect(const char *file, int line, const sim_t *s, bool on, sh_pump_reason_t reason)
{
    t_checks++;
    if (s->st.on != on || s->st.reason != reason) {
        t_failed++;
        printf("FAIL %s:%d: pump %s (%s), expected %s (%s)\n", file, line,
               s->st.on ? "ON" : "OFF", sh_pump_reason_name(s->st.reason),
               on ? "ON" : "OFF", sh_pump_reason_name(reason));
    }
}

#define EXPECT_ON(s, reason)    expect(__FILE__, __LINE__, &(s), true, SH_PUMP_REASON_##reason)
#define EXPECT_OFF(s, reason)   expect(__FILE__, __LINE__, &(s), false, SH_PUMP_REASON_##reason)

/* Pump running by the difference (collector 50.0, tank 40.0), minimum run time over. */
static sim_t sim_running(void)
{
    sim_t s = sim_off();
    step(&s, 0, 500, 400);
    EXPECT_ON(s, DIFF);
    s.now_s += LONG_AGO;
    return s;
}

/* ---- Difference ---------------------------------------------------------------------- */

static void test_diff_on_threshold(void)
{
    sim_t s = sim_off();
    step(&s, 1, 459, 400);          /* 5.9 K */
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 460, 400);          /* exactly 6.0 K */
    EXPECT_ON(s, DIFF);

    s = sim_off();
    step(&s, 1, 461, 400);          /* 6.1 K */
    EXPECT_ON(s, DIFF);
}

static void test_diff_off_threshold(void)
{
    sim_t s = sim_running();
    step(&s, 1, 441, 400);          /* 4.1 K */
    EXPECT_ON(s, DIFF);
    step(&s, 1, 440, 400);          /* exactly 4.0 K */
    EXPECT_OFF(s, DIFF);

    s = sim_running();
    step(&s, 1, 439, 400);          /* 3.9 K */
    EXPECT_OFF(s, DIFF);
}

static void test_diff_between_thresholds_keeps_state(void)
{
    sim_t s = sim_off();
    step(&s, 1, 450, 400);          /* 5.0 K: a stopped pump stays stopped */
    EXPECT_OFF(s, DIFF);

    s = sim_running();
    step(&s, 1, 450, 400);          /* 5.0 K: a running pump keeps running */
    EXPECT_ON(s, DIFF);
}

static void test_diff_negative_temperatures(void)
{
    sim_t s = sim_off();
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = -300, .col_protect = 1100 };
    step(&s, 1, -41, -100);         /* 5.9 K */
    EXPECT_OFF(s, DIFF);
    step(&s, 1, -40, -100);         /* 6.0 K */
    EXPECT_ON(s, DIFF);
}

/* ---- Minimum run time ---------------------------------------------------------------- */

static void test_min_run_time(void)
{
    sim_t s = sim_off();
    step(&s, 0, 500, 400);
    EXPECT_ON(s, DIFF);
    step(&s, 1, 420, 400);          /* 2.0 K after 1 s: held */
    EXPECT_ON(s, MIN_RUN);
    step(&s, 178, 420, 400);        /* 179 s */
    EXPECT_ON(s, MIN_RUN);
    step(&s, 1, 420, 400);          /* exactly 180 s */
    EXPECT_OFF(s, DIFF);
}

static void test_min_run_time_difference_recovers(void)
{
    sim_t s = sim_off();
    step(&s, 0, 500, 400);
    step(&s, 60, 420, 400);
    EXPECT_ON(s, MIN_RUN);
    step(&s, 60, 441, 400);         /* back above the off difference */
    EXPECT_ON(s, DIFF);
    step(&s, 600, 441, 400);        /* long after the minimum: still running */
    EXPECT_ON(s, DIFF);
}

/* ---- Minimum off time ---------------------------------------------------------------- */

static void test_min_off_time(void)
{
    sim_t s = sim_running();
    step(&s, 1, 430, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 500, 400);          /* 10 K after 1 s: held */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 118, 500, 400);        /* 119 s */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 1, 500, 400);          /* exactly 120 s */
    EXPECT_ON(s, DIFF);
}

static void test_min_off_time_counts_from_start(void)
{
    sim_t s = { .p = &P };          /* power-on: pump off for 0 s */
    step(&s, 1, 500, 400);
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 119, 500, 400);
    EXPECT_ON(s, DIFF);
}

static void test_min_off_time_zero_disables_it(void)
{
    sim_t s = sim_running();
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 0,
                               .tank_max = 700, .col_min = 250, .col_protect = 1100 };
    step(&s, 1, 430, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 0, 500, 400);
    EXPECT_ON(s, DIFF);
}

/* ---- Minimum collector temperature --------------------------------------------------- */

static void test_col_min_blocks_start(void)
{
    sim_t s = sim_off();
    step(&s, 1, 249, 100);          /* 24.9 °C, 14.9 K */
    EXPECT_OFF(s, COL_MIN);
    step(&s, 1, 250, 100);          /* exactly 25.0 °C */
    EXPECT_ON(s, DIFF);

    s = sim_off();
    step(&s, 1, 251, 100);
    EXPECT_ON(s, DIFF);
}

static void test_col_min_no_return_zone(void)
{
    sim_t s = sim_off();
    step(&s, 1, 260, 250);          /* above the minimum, no difference */
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 249, 100);          /* 0.1 K below: blocked again at once */
    EXPECT_OFF(s, COL_MIN);
}

static void test_col_min_does_not_stop_running_pump(void)
{
    sim_t s = sim_off();
    step(&s, 1, 300, 100);
    EXPECT_ON(s, DIFF);
    step(&s, 600, 200, 100);        /* 20.0 °C, 10 K: keeps running */
    EXPECT_ON(s, DIFF);
    step(&s, 1, 140, 100);          /* stops by the difference only */
    EXPECT_OFF(s, DIFF);
}

/* ---- Tank Tmax ----------------------------------------------------------------------- */

static void test_tank_max_threshold(void)
{
    sim_t s = sim_off();
    step(&s, 1, 800, 699);          /* 69.9 °C */
    EXPECT_ON(s, DIFF);

    s = sim_off();
    step(&s, 1, 800, 700);          /* exactly 70.0 °C */
    EXPECT_OFF(s, TANK_MAX);

    s = sim_off();
    step(&s, 1, 800, 701);
    EXPECT_OFF(s, TANK_MAX);
}

static void test_tank_max_stops_at_once(void)
{
    sim_t s = sim_off();
    step(&s, 0, 800, 699);
    EXPECT_ON(s, DIFF);
    step(&s, 1, 800, 700);          /* 1 s of run: the minimum run time does not hold it */
    EXPECT_OFF(s, TANK_MAX);
}

static void test_tank_max_return_zone(void)
{
    sim_t s = sim_running();
    step(&s, 1, 800, 700);
    EXPECT_OFF(s, TANK_MAX);
    step(&s, LONG_AGO, 800, 699);
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 1, 800, 681);          /* 68.1 °C */
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 1, 800, 680);          /* exactly Tmax - 2 K: not yet below */
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 1, 800, 679);          /* 67.9 °C */
    EXPECT_ON(s, DIFF);
    step(&s, 1, 800, 699);          /* in the zone, coming from below: charging goes on */
    EXPECT_ON(s, DIFF);
}

static void test_tank_max_restart_waits_min_off_time(void)
{
    sim_t s = sim_running();
    step(&s, 1, 800, 700);
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 60, 800, 679);         /* released after 60 s: the difference rule waits */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 60, 800, 679);
    EXPECT_ON(s, DIFF);
}

/* ---- Tank emergency ceiling ---------------------------------------------------------- */

static void test_tank_ceiling_threshold(void)
{
    sim_t s = sim_off();
    step(&s, 1, 1120, 949);         /* 94.9 °C: collector protection may still charge */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1120, 950);         /* exactly 95.0 °C: off at once, 1 s after the start */
    EXPECT_OFF(s, TANK_CEILING);

    s = sim_off();
    step(&s, 1, 1120, 951);
    EXPECT_OFF(s, TANK_CEILING);
}

static void test_tank_ceiling_return_zone(void)
{
    sim_t s = sim_off();
    step(&s, 1, 1120, 950);
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, LONG_AGO, 1120, 949);
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 1, 1120, 930);         /* exactly 93.0 °C: not yet below */
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 1, 1120, 929);         /* 92.9 °C */
    EXPECT_ON(s, COL_PROTECT);
}

static void test_tank_ceiling_wins_over_tank_max(void)
{
    sim_t s = sim_off();
    step(&s, 1, 1000, 950);
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 1, 1000, 929);         /* ceiling released, Tmax still holds */
    EXPECT_OFF(s, TANK_MAX);
}

/* ---- Collector protection ------------------------------------------------------------ */

static void test_col_protect_threshold(void)
{
    sim_t s = sim_off();
    step(&s, 1, 1099, 800);         /* 109.9 °C, tank above Tmax */
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 1, 1100, 800);         /* exactly 110.0 °C */
    EXPECT_ON(s, COL_PROTECT);

    s = sim_off();
    step(&s, 1, 1101, 800);
    EXPECT_ON(s, COL_PROTECT);
}

static void test_col_protect_ignores_min_off_time(void)
{
    sim_t s = sim_running();
    step(&s, 1, 430, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 1100, 400);         /* 1 s after the stop */
    EXPECT_ON(s, COL_PROTECT);
}

static void test_col_protect_return_zone_and_min_run(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1100, 800);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1070, 800);         /* below threshold - 2 K, but only 1 s of run */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 178, 1070, 800);       /* 179 s */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1080, 800);         /* 180 s, exactly 108.0 °C: not yet below */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1079, 800);         /* 107.9 °C: protection ends, tank above Tmax */
    EXPECT_OFF(s, TANK_MAX);
}

static void test_col_protect_zone_does_not_start(void)
{
    sim_t s = sim_off();
    step(&s, 1, 1090, 800);         /* inside the zone, coming from below */
    EXPECT_OFF(s, TANK_MAX);
}

static void test_col_protect_hands_over_to_difference(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1100, 600);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 180, 1079, 600);       /* protection over, tank below Tmax: normal charging */
    EXPECT_ON(s, DIFF);
}

static void test_col_protect_takes_over_running_pump(void)
{
    sim_t s = sim_running();        /* running by the difference for a long time */
    step(&s, 1, 1100, 690);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1100, 720);         /* tank passes Tmax: protection keeps charging */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1079, 720);         /* the pump has run long enough: ends at once */
    EXPECT_OFF(s, TANK_MAX);
}

static void test_col_protect_stops_at_tank_ceiling(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1200, 940);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1200, 950);         /* within the minimum run time */
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, LONG_AGO, 1200, 940);  /* stagnation: no cycling until the tank is below 93 °C */
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 1, 1090, 929);         /* released with the collector inside its zone: the */
    EXPECT_OFF(s, TANK_MAX);        /* protection ended with the stop, no start below 110 °C */
    step(&s, 1, 1100, 929);
    EXPECT_ON(s, COL_PROTECT);
}

static void test_col_protect_cap(void)
{
    sim_t s = sim_off();
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 1600 };
    step(&s, 1, 1149, 800);         /* 114.9 °C */
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 1, 1150, 800);         /* the 115 °C cap, not the 160 °C that was set */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 180, 1130, 800);       /* exactly cap - 2 K */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1129, 800);
    EXPECT_OFF(s, TANK_MAX);
}

static void test_col_protect_below_cap_is_used_as_set(void)
{
    sim_t s = sim_off();
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 900 };
    step(&s, 1, 899, 800);
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 1, 900, 800);
    EXPECT_ON(s, COL_PROTECT);
}

/* ---- No start with the collector at the boiling point -------------------------------- */

static void test_col_boil_threshold(void)
{
    sim_t s = sim_off();
    step(&s, 1, 1269, 400);         /* 126.9 °C: collector protection starts the pump */
    EXPECT_ON(s, COL_PROTECT);

    s = sim_off();
    step(&s, 1, 1270, 400);         /* exactly 127.0 °C */
    EXPECT_OFF(s, COL_BOILING);

    s = sim_off();
    step(&s, 1, 1271, 400);
    EXPECT_OFF(s, COL_BOILING);
}

static void test_col_boil_return_zone(void)
{
    sim_t s = sim_off();
    step(&s, 1, 1270, 400);
    EXPECT_OFF(s, COL_BOILING);
    step(&s, LONG_AGO, 1269, 400);
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1250, 400);         /* exactly 125.0 °C: not yet below */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1249, 400);         /* 124.9 °C */
    EXPECT_ON(s, COL_PROTECT);
}

static void test_col_boil_stops_running_pump(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1200, 400);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1269, 400);         /* 126.9 °C, the collector keeps heating with the pump running */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1270, 400);         /* boiling point, 2 s after the start: off at once */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, LONG_AGO, 1250, 400);  /* exactly 125.0 °C: not yet below */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1249, 400);         /* released: the protection starts at once (no minimum off time) */
    EXPECT_ON(s, COL_PROTECT);

    s = sim_running();              /* running by the difference */
    step(&s, 1, 1270, 400);
    EXPECT_OFF(s, COL_BOILING);
}

static void test_col_boil_before_tank_ceiling(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1200, 940);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1300, 950);         /* both limits at once: the boiling point is checked first */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, LONG_AGO, 1260, 900);  /* tank released, collector still in its zone */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1249, 900);
    EXPECT_ON(s, COL_PROTECT);

    s = sim_off();
    step(&s, 0, 1200, 940);
    step(&s, 1, 1200, 950);         /* the ceiling first... */
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 1, 1300, 950);         /* ...then the collector passes the boiling point */
    EXPECT_OFF(s, COL_BOILING);
}

/* ---- Invalid temperature ------------------------------------------------------------- */

static void test_sensor_invalid_no_start(void)
{
    sim_t s = sim_off();
    step(&s, 1, SH_TEMP_INVALID, 400);
    EXPECT_OFF(s, SENSOR);
    step(&s, 1, 800, SH_TEMP_INVALID);
    EXPECT_OFF(s, SENSOR);
    step(&s, 1, SH_TEMP_INVALID, SH_TEMP_INVALID);
    EXPECT_OFF(s, SENSOR);
}

static void test_sensor_invalid_stops_at_once(void)
{
    sim_t s = sim_off();
    step(&s, 0, 500, 400);
    EXPECT_ON(s, DIFF);
    step(&s, 1, 500, SH_TEMP_INVALID);      /* within the minimum run time */
    EXPECT_OFF(s, SENSOR);

    s = sim_off();
    step(&s, 0, 1200, 400);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, SH_TEMP_INVALID, 400);      /* collector protection is stopped as well */
    EXPECT_OFF(s, SENSOR);
}

static void test_sensor_recovery(void)
{
    sim_t s = sim_running();
    step(&s, 1, SH_TEMP_INVALID, 400);
    EXPECT_OFF(s, SENSOR);
    step(&s, 10, 500, 400);                 /* valid again: the minimum off time applies */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 110, 500, 400);
    EXPECT_ON(s, DIFF);

    s = sim_off();
    step(&s, 0, 1200, 400);
    step(&s, 1, SH_TEMP_INVALID, 400);
    step(&s, 1, 1070, 400);                 /* protection did not survive the fault... */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 1, 1100, 400);                 /* ...and starts again only at its threshold */
    EXPECT_ON(s, COL_PROTECT);
}

static void test_sensor_invalid_keeps_limits(void)
{
    sim_t s = sim_running();
    step(&s, 1, 800, 700);
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 1, SH_TEMP_INVALID, SH_TEMP_INVALID);
    EXPECT_OFF(s, SENSOR);
    step(&s, LONG_AGO, 800, 690);           /* still inside the Tmax zone */
    EXPECT_OFF(s, TANK_MAX);
}

static void test_sensor_invalid_limits_follow_valid_sensor(void)
{
    sim_t s = sim_off();
    step(&s, 1, SH_TEMP_INVALID, 700);      /* tank reaches Tmax while the collector is invalid */
    EXPECT_OFF(s, SENSOR);
    step(&s, 1, 800, 690);                  /* the Tmax limit was latched all the same */
    EXPECT_OFF(s, TANK_MAX);

    s = sim_off();
    step(&s, 1, 1270, SH_TEMP_INVALID);     /* collector at the boiling point, tank invalid */
    EXPECT_OFF(s, SENSOR);
    step(&s, 1, 1260, 400);
    EXPECT_OFF(s, COL_BOILING);
}

/* ---- Priorities ---------------------------------------------------------------------- */

static void test_priority_order(void)
{
    /* sensor fault > everything */
    sim_t s = sim_off();
    step(&s, 1, SH_TEMP_INVALID, 960);
    EXPECT_OFF(s, SENSOR);

    /* boiling point > tank ceiling */
    s = sim_off();
    step(&s, 1, 1270, 950);
    EXPECT_OFF(s, COL_BOILING);

    /* tank ceiling > collector protection */
    s = sim_off();
    step(&s, 1, 1100, 950);
    EXPECT_OFF(s, TANK_CEILING);

    /* collector protection > Tmax */
    s = sim_off();
    step(&s, 1, 1100, 700);
    EXPECT_ON(s, COL_PROTECT);

    /* Tmax > difference */
    s = sim_off();
    step(&s, 1, 900, 700);
    EXPECT_OFF(s, TANK_MAX);
}

static void test_min_times_only_for_difference(void)
{
    /* every protection stops within the minimum run time */
    sim_t s = sim_off();
    step(&s, 0, 800, 690);
    step(&s, 1, 800, 700);
    EXPECT_OFF(s, TANK_MAX);

    s = sim_off();
    step(&s, 0, 990, 940);
    step(&s, 1, 990, 950);
    EXPECT_OFF(s, TANK_CEILING);

    /* collector protection starts within the minimum off time, also right after the start */
    s = (sim_t){ .p = &P };
    step(&s, 0, 1100, 400);
    EXPECT_ON(s, COL_PROTECT);
}

static void test_initial_state(void)
{
    sim_t s = { .p = &P };
    CHECK(!s.st.on && s.st.reason == SH_PUMP_REASON_NONE);
}

static void test_reason_names(void)
{
    for (int r = 0; r < SH_PUMP_REASON_COUNT; r++) {
        CHECK(strcmp(sh_pump_reason_name((sh_pump_reason_t)r), "?") != 0);
    }
    CHECK(strcmp(sh_pump_reason_name(SH_PUMP_REASON_COUNT), "?") == 0);
}

/* ---- Pump test (step 2, 3.3) --------------------------------------------------------- */

/* The buttons on the normal screen, followed at once by a decision (as sh_control does). */
static void key_plus(sim_t *s)
{
    const sh_pump_input_t in = { s->t_col, s->t_tank, s->col_fault, s->tank_fault };
    sh_pump_test_plus(s->p, &s->st, &in, s->now_s);
    step(s, 0, s->t_col, s->t_tank);
}

static bool key_minus(sim_t *s)
{
    const bool taken = sh_pump_test_minus(&s->st, s->now_s);
    step(s, 0, s->t_col, s->t_tank);
    return taken;
}

static bool key_menu(sim_t *s)
{
    const bool taken = sh_pump_test_stop(&s->st, s->now_s);
    step(s, 0, s->t_col, s->t_tank);
    return taken;
}

static uint32_t test_left(const sim_t *s)
{
    return sh_pump_test_left(&s->st, s->now_s);
}

/* Pump off, no rule would start it (collector 30.0, tank 40.0). */
static sim_t sim_idle(void)
{
    sim_t s = sim_off();
    step(&s, 0, 300, 400);
    EXPECT_OFF(s, DIFF);
    return s;
}

static void test_test_starts_at_once(void)
{
    sim_t s = sim_running();
    step(&s, 1, 300, 400);
    EXPECT_OFF(s, DIFF);
    CHECK(test_left(&s) == 0);
    step(&s, 1, 300, 400);          /* 1 s after the stop: no minimum off time for the test */
    key_plus(&s);
    EXPECT_ON(s, TEST);
    CHECK(test_left(&s) == 300);
}

static void test_test_runs_for_the_set_time(void)
{
    sim_t s = sim_idle();
    key_plus(&s);
    step(&s, 1, 300, 400);
    EXPECT_ON(s, TEST);
    CHECK(test_left(&s) == 299);
    step(&s, 298, 300, 400);        /* 299 s */
    EXPECT_ON(s, TEST);
    CHECK(test_left(&s) == 1);
    step(&s, 1, 300, 400);          /* exactly 300 s */
    EXPECT_OFF(s, DIFF);
    CHECK(test_left(&s) == 0);

    s = sim_idle();
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 1100, .test_s = 60 };
    key_plus(&s);
    CHECK(test_left(&s) == 60);
    step(&s, 59, 300, 400);
    EXPECT_ON(s, TEST);
    step(&s, 1, 300, 400);
    EXPECT_OFF(s, DIFF);
}

static void test_test_ignores_the_other_rules(void)
{
    sim_t s = sim_off();
    step(&s, 0, 900, 700);          /* tank at Tmax */
    EXPECT_OFF(s, TANK_MAX);
    key_plus(&s);
    EXPECT_ON(s, TEST);

    s = sim_off();
    step(&s, 0, 100, 400);          /* collector colder than the tank and below its minimum */
    key_plus(&s);
    EXPECT_ON(s, TEST);

    s = sim_off();
    step(&s, 0, SH_TEMP_INVALID, SH_TEMP_INVALID);  /* no values yet */
    EXPECT_OFF(s, SENSOR);
    key_plus(&s);
    EXPECT_ON(s, TEST);

    s = sim_off();
    s.col_fault = s.tank_fault = true;              /* both sensors faulty */
    step(&s, 0, 300, 400);
    EXPECT_OFF(s, SENSOR);
    key_plus(&s);
    EXPECT_ON(s, TEST);
    step(&s, 299, 300, 400);
    EXPECT_ON(s, TEST);
    step(&s, 1, 300, 400);
    EXPECT_OFF(s, SENSOR);
}

static void test_test_refused_by_the_two_limits(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1270, 400);         /* collector at the boiling point */
    key_plus(&s);
    EXPECT_OFF(s, COL_BOILING);
    CHECK(test_left(&s) == 0);
    step(&s, 1, 1250, 400);         /* still inside the return zone */
    key_plus(&s);
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1269, 400);
    s = sim_off();
    step(&s, 0, 1269, 400);         /* 0.1 K below: the test takes over the collector protection */
    EXPECT_ON(s, COL_PROTECT);
    key_plus(&s);
    EXPECT_ON(s, TEST);

    s = sim_off();
    step(&s, 0, 300, 950);          /* tank at the emergency ceiling */
    key_plus(&s);
    EXPECT_OFF(s, TANK_CEILING);
    CHECK(test_left(&s) == 0);
    s = sim_off();
    step(&s, 0, 300, 949);
    key_plus(&s);
    EXPECT_ON(s, TEST);
}

static void test_test_stopped_by_the_two_limits(void)
{
    sim_t s = sim_idle();
    key_plus(&s);
    step(&s, 10, 1269, 400);
    EXPECT_ON(s, TEST);
    step(&s, 1, 1270, 400);         /* 11 s after the start: off at once, the test is over */
    EXPECT_OFF(s, COL_BOILING);
    CHECK(test_left(&s) == 0);
    step(&s, 1, 1249, 400);         /* released: the test does not come back */
    EXPECT_ON(s, COL_PROTECT);

    s = sim_idle();
    key_plus(&s);
    step(&s, 10, 300, 949);
    EXPECT_ON(s, TEST);
    step(&s, 1, 300, 950);
    EXPECT_OFF(s, TANK_CEILING);
    CHECK(test_left(&s) == 0);
    step(&s, 1, 300, 929);
    EXPECT_OFF(s, TANK_MAX);
}

static void test_test_limit_of_a_faulty_sensor_cannot_be_checked(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1270, 400);
    EXPECT_OFF(s, COL_BOILING);
    s.col_fault = true;             /* the 127.0 °C is now an old value */
    step(&s, 1, 1270, 400);
    key_plus(&s);
    EXPECT_ON(s, TEST);

    s = sim_off();
    step(&s, 0, 300, 950);
    EXPECT_OFF(s, TANK_CEILING);
    s.tank_fault = true;
    step(&s, 1, 300, 950);
    key_plus(&s);
    EXPECT_ON(s, TEST);

    s = sim_off();
    s.tank_fault = true;            /* the limit of the good sensor still counts */
    step(&s, 0, 1270, 400);
    key_plus(&s);
    EXPECT_OFF(s, COL_BOILING);
}

static void test_test_plus_adds_a_minute(void)
{
    sim_t s = sim_idle();
    key_plus(&s);
    step(&s, 10, 300, 400);
    CHECK(test_left(&s) == 290);
    key_plus(&s);
    CHECK(test_left(&s) == 350);
    step(&s, 349, 300, 400);
    EXPECT_ON(s, TEST);
    step(&s, 1, 300, 400);
    EXPECT_OFF(s, DIFF);

    s = sim_idle();                 /* the time left is at most 900 s */
    key_plus(&s);
    for (int i = 0; i < 9; i++) {
        key_plus(&s);
    }
    CHECK(test_left(&s) == 840);
    key_plus(&s);
    CHECK(test_left(&s) == 900);
    key_plus(&s);
    CHECK(test_left(&s) == 900);
    step(&s, 41, 300, 400);
    CHECK(test_left(&s) == 859);
    key_plus(&s);                   /* 859 + 60 would be 919 */
    CHECK(test_left(&s) == 900);
    step(&s, 899, 300, 400);
    EXPECT_ON(s, TEST);
    step(&s, 1, 300, 400);
    EXPECT_OFF(s, DIFF);
}

static void test_test_minus_takes_a_minute(void)
{
    sim_t s = sim_idle();
    CHECK(!key_minus(&s));          /* no test: "-" does nothing */
    EXPECT_OFF(s, DIFF);

    key_plus(&s);
    CHECK(key_minus(&s));
    CHECK(test_left(&s) == 240);
    step(&s, 179, 300, 400);
    CHECK(test_left(&s) == 61);
    CHECK(key_minus(&s));           /* 61 s left: more than 60 */
    CHECK(test_left(&s) == 1);
    EXPECT_ON(s, TEST);
    CHECK(key_minus(&s));           /* 1 s left: no change, never down to 0 */
    CHECK(test_left(&s) == 1);
    step(&s, 1, 300, 400);
    EXPECT_OFF(s, DIFF);

    s = sim_idle();
    key_plus(&s);
    step(&s, 240, 300, 400);
    CHECK(test_left(&s) == 60);
    CHECK(key_minus(&s));           /* exactly 60 s left: no change */
    CHECK(test_left(&s) == 60);
}

static void test_test_menu_ends_it(void)
{
    sim_t s = sim_idle();
    CHECK(!key_menu(&s));           /* no test: "menu" is for the menu */

    key_plus(&s);
    step(&s, 100, 300, 400);
    EXPECT_ON(s, TEST);
    CHECK(key_menu(&s));            /* after the first minute: off at once */
    EXPECT_OFF(s, DIFF);
    CHECK(test_left(&s) == 0);
    CHECK(!key_menu(&s));
}

static void test_test_runs_at_least_a_minute(void)
{
    sim_t s = sim_idle();
    key_plus(&s);
    step(&s, 10, 300, 400);
    CHECK(key_menu(&s));            /* 10 s after the start: the pump still runs to the full minute */
    EXPECT_ON(s, TEST);
    CHECK(test_left(&s) == 50);
    CHECK(key_menu(&s));
    step(&s, 49, 300, 400);         /* 59 s */
    EXPECT_ON(s, TEST);
    step(&s, 1, 300, 400);          /* exactly 60 s */
    EXPECT_OFF(s, DIFF);

    s = sim_idle();                 /* the same when the set time runs out earlier */
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 1100, .test_s = 90 };
    key_plus(&s);
    step(&s, 10, 300, 400);
    CHECK(key_minus(&s));           /* 80 s -> 20 s: the set time ends 30 s after the start */
    CHECK(test_left(&s) == 50);
    step(&s, 49, 300, 400);
    EXPECT_ON(s, TEST);
    step(&s, 1, 300, 400);
    EXPECT_OFF(s, DIFF);

    s = sim_idle();                 /* "+" after "menu": the test goes on */
    key_plus(&s);
    step(&s, 10, 300, 400);
    key_menu(&s);
    key_plus(&s);
    CHECK(test_left(&s) == 60);
    step(&s, 59, 300, 400);
    EXPECT_ON(s, TEST);
    step(&s, 1, 300, 400);
    EXPECT_OFF(s, DIFF);
}

static void test_test_end_pump_stays_on_by_a_rule(void)
{
    sim_t s = sim_running();
    step(&s, 1, 430, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 500, 400);          /* enough difference, but stopped 1 s ago */
    EXPECT_OFF(s, MIN_OFF);
    key_plus(&s);
    EXPECT_ON(s, TEST);
    step(&s, 300, 500, 400);        /* the end: as from a stopped pump, minimum off time over */
    EXPECT_ON(s, DIFF);
    step(&s, 1, 420, 400);          /* a new run: its minimum run time counts from the end of the test */
    EXPECT_ON(s, MIN_RUN);
    step(&s, 178, 420, 400);
    EXPECT_ON(s, MIN_RUN);
    step(&s, 1, 420, 400);
    EXPECT_OFF(s, DIFF);

    s = sim_idle();                 /* collector protection */
    key_plus(&s);
    step(&s, 300, 1100, 800);
    EXPECT_ON(s, COL_PROTECT);

    s = sim_idle();                 /* inside the zone of the difference: a stopped pump does not start */
    key_plus(&s);
    step(&s, 300, 450, 400);
    EXPECT_OFF(s, DIFF);

    s = sim_idle();                 /* inside the zone of the protection: the same */
    key_plus(&s);
    step(&s, 300, 1090, 800);
    EXPECT_OFF(s, TANK_MAX);
}

static void test_test_end_off_time_counts_from_the_end(void)
{
    sim_t s = sim_idle();
    key_plus(&s);
    step(&s, 300, 300, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 500, 400);
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 118, 500, 400);        /* 119 s after the end */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 1, 500, 400);
    EXPECT_ON(s, DIFF);
}

static void test_test_takes_over_a_running_pump(void)
{
    sim_t s = sim_running();
    key_plus(&s);
    EXPECT_ON(s, TEST);
    step(&s, 100, 300, 400);        /* no difference any more: the test keeps it running */
    EXPECT_ON(s, TEST);
    step(&s, 200, 300, 400);
    EXPECT_OFF(s, DIFF);
}

/* ---- Anti-seize (step 2, 3.4) -------------------------------------------------------- */

#define DAY     (24 * HOUR)

static void test_seize_after_24_h(void)
{
    sim_t s = { .p = &P };          /* power-on: the 24 h count from the start */
    step(&s, DAY - 1, 400, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 400, 400);          /* exactly 24 h, collector as warm as the tank */
    EXPECT_ON(s, ANTI_SEIZE);
    step(&s, 59, 400, 400);
    EXPECT_ON(s, ANTI_SEIZE);
    step(&s, 1, 400, 400);          /* exactly 60 s */
    EXPECT_OFF(s, DIFF);

    step(&s, DAY - 1, 400, 400);    /* the next one 24 h after this stop */
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 400, 400);
    EXPECT_ON(s, ANTI_SEIZE);
}

static void test_seize_waits_for_collector_not_colder(void)
{
    sim_t s = { .p = &P };
    step(&s, DAY, 399, 400);        /* collector 0.1 K colder: it would cool the tank */
    EXPECT_OFF(s, DIFF);
    step(&s, HOUR, 399, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 400, 400);
    EXPECT_ON(s, ANTI_SEIZE);

    s = (sim_t){ .p = &P };
    step(&s, DAY, 401, 400);
    EXPECT_ON(s, ANTI_SEIZE);
}

static void test_seize_after_12_h_more_whatever_the_temperatures(void)
{
    sim_t s = { .p = &P };
    step(&s, DAY, 100, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 12 * HOUR - 1, 100, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 100, 400);          /* exactly 36 h */
    EXPECT_ON(s, ANTI_SEIZE);
    step(&s, 60, 100, 400);
    EXPECT_OFF(s, DIFF);
}

static void test_seize_any_run_restarts_the_24_h(void)
{
    sim_t s = { .p = &P };
    step(&s, 12 * HOUR, 500, 400);  /* a run by the difference */
    EXPECT_ON(s, DIFF);
    step(&s, 600, 400, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, DAY - 1, 400, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 400, 400);
    EXPECT_ON(s, ANTI_SEIZE);

    s = (sim_t){ .p = &P };         /* a pump test counts too */
    step(&s, 12 * HOUR, 400, 400);
    key_plus(&s);
    step(&s, 300, 400, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, DAY - 1, 400, 400);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 400, 400);
    EXPECT_ON(s, ANTI_SEIZE);
}

static void test_seize_ignores_the_other_rules(void)
{
    sim_t s = { .p = &P };
    step(&s, DAY, 800, 720);        /* tank above Tmax */
    EXPECT_ON(s, ANTI_SEIZE);
    step(&s, 60, 800, 720);
    EXPECT_OFF(s, TANK_MAX);

    s = (sim_t){ .p = &P };         /* both sensors faulty: no temperatures, so after 36 h */
    s.col_fault = s.tank_fault = true;
    step(&s, DAY, 800, 400);
    EXPECT_OFF(s, SENSOR);
    step(&s, 12 * HOUR - 1, 800, 400);
    EXPECT_OFF(s, SENSOR);
    step(&s, 1, 800, 400);
    EXPECT_ON(s, ANTI_SEIZE);
    step(&s, 60, 800, 400);
    EXPECT_OFF(s, SENSOR);

    s = (sim_t){ .p = &P };         /* no values at all */
    step(&s, 36 * HOUR, SH_TEMP_INVALID, SH_TEMP_INVALID);
    EXPECT_ON(s, ANTI_SEIZE);
}

static void test_seize_refused_and_stopped_by_the_two_limits(void)
{
    sim_t s = { .p = &P };
    step(&s, DAY, 1270, 400);       /* collector at the boiling point */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 12 * HOUR, 1270, 400);
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1000, 990);         /* released, and now the tank at the ceiling */
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 1, 1000, 929);         /* released: it runs at last */
    EXPECT_ON(s, ANTI_SEIZE);
    step(&s, 30, 1000, 950);        /* stopped at once */
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 1, 1000, 929);         /* the pump has run: 24 h again */
    EXPECT_OFF(s, TANK_MAX);
}

static void test_seize_end_as_the_end_of_the_test(void)
{
    sim_t s = { .p = &P };
    step(&s, DAY, 400, 400);
    EXPECT_ON(s, ANTI_SEIZE);
    step(&s, 60, 460, 400);         /* the end: the difference keeps the pump on */
    EXPECT_ON(s, DIFF);
    step(&s, 179, 420, 400);
    EXPECT_ON(s, MIN_RUN);
    step(&s, 1, 420, 400);
    EXPECT_OFF(s, DIFF);
}

static void test_seize_taken_over_by_the_test(void)
{
    sim_t s = { .p = &P };
    step(&s, DAY, 400, 400);
    EXPECT_ON(s, ANTI_SEIZE);
    step(&s, 30, 400, 400);
    key_plus(&s);
    EXPECT_ON(s, TEST);
    CHECK(test_left(&s) == 300);
    step(&s, 299, 400, 400);
    EXPECT_ON(s, TEST);
    step(&s, 1, 400, 400);
    EXPECT_OFF(s, DIFF);
}

/* ---- Faulty sensors (step 2, 3.5) ---------------------------------------------------- */

static void test_fault_both_sensors(void)
{
    sim_t s = sim_running();
    s.col_fault = s.tank_fault = true;
    step(&s, 1, 500, 400);          /* old values with enough difference: off all the same */
    EXPECT_OFF(s, SENSOR);
    step(&s, LONG_AGO, 1200, 400);
    EXPECT_OFF(s, SENSOR);
}

static void test_fault_sensor_without_a_value(void)
{
    sim_t s = sim_off();
    s.col_fault = true;             /* collector faulty, tank never answered and not yet declared */
    step(&s, 1, 500, SH_TEMP_INVALID);
    EXPECT_OFF(s, SENSOR);
    s.tank_fault = true;            /* declared: both faulty */
    step(&s, 1, 500, SH_TEMP_INVALID);
    EXPECT_OFF(s, SENSOR);

    s = sim_off();
    s.tank_fault = true;            /* a tank sensor that never gave a value, declared faulty */
    step(&s, 1, 600, SH_TEMP_INVALID);
    EXPECT_ON(s, TANK_FAULT);
}

/* -- tank sensor faulty -- */

static sim_t sim_tank_fault(void)
{
    sim_t s = sim_off();
    s.tank_fault = true;
    return s;
}

static void test_tank_fault_start_threshold(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 1, 599, 200);          /* 59.9 °C */
    EXPECT_OFF(s, TANK_FAULT);
    step(&s, 1, 600, 200);          /* exactly 60.0 °C */
    EXPECT_ON(s, TANK_FAULT);

    s = sim_tank_fault();
    step(&s, 1, 601, 200);
    EXPECT_ON(s, TANK_FAULT);
}

static void test_tank_fault_min_off_time(void)
{
    sim_t s = sim_running();
    step(&s, 1, 430, 400);
    EXPECT_OFF(s, DIFF);
    s.tank_fault = true;
    step(&s, 1, 700, 400);
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 118, 700, 400);        /* 119 s after the stop */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 1, 700, 400);
    EXPECT_ON(s, TANK_FAULT);
}

static void test_tank_fault_stop_threshold_and_min_run(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 0, 700, 200);
    EXPECT_ON(s, TANK_FAULT);
    step(&s, 1, 579, 200);          /* 57.9 °C after 1 s: held */
    EXPECT_ON(s, MIN_RUN);
    step(&s, 178, 579, 200);
    EXPECT_ON(s, MIN_RUN);
    step(&s, 1, 580, 200);          /* 180 s, exactly 58.0 °C: not yet below */
    EXPECT_ON(s, TANK_FAULT);
    step(&s, 1, 579, 200);
    EXPECT_OFF(s, TANK_FAULT);
    step(&s, LONG_AGO, 599, 200);   /* 58 - 60 °C: a stopped pump stays stopped */
    EXPECT_OFF(s, TANK_FAULT);
    s = sim_tank_fault();
    step(&s, 0, 700, 200);
    step(&s, 179, 579, 200);
    EXPECT_ON(s, MIN_RUN);
    step(&s, 1, 579, 200);          /* exactly 180 s */
    EXPECT_OFF(s, TANK_FAULT);
}

static void test_tank_fault_collector_95(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 0, 900, 200);
    EXPECT_ON(s, TANK_FAULT);
    step(&s, 1, 949, 200);          /* 94.9 °C */
    EXPECT_ON(s, TANK_FAULT);
    step(&s, 1, 950, 200);          /* exactly 95.0 °C, 2 s of run: off at once */
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, LONG_AGO, 949, 200);
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 1, 930, 200);          /* exactly 93.0 °C: not yet below */
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 1, 929, 200);          /* 92.9 °C */
    EXPECT_ON(s, TANK_FAULT);

    s = sim_tank_fault();           /* a stopped pump is not started above 95 °C to be stopped at once */
    step(&s, 1, 1000, 200);
    EXPECT_OFF(s, TANK_FAULT_HOT);
}

static void test_tank_fault_restart_after_95_waits_min_off_time(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 0, 900, 200);
    step(&s, 300, 950, 200);
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 60, 929, 200);
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 60, 929, 200);
    EXPECT_ON(s, TANK_FAULT);
}

static void test_tank_fault_tmax_and_ceiling_not_used(void)
{
    sim_t s = sim_off();
    step(&s, 1, 800, 950);          /* the last tank values: at the ceiling (and above Tmax) */
    EXPECT_OFF(s, TANK_CEILING);
    s.tank_fault = true;
    step(&s, 1, 800, 950);          /* that value is old now: the collector alone decides */
    EXPECT_ON(s, TANK_FAULT);
    step(&s, 1, 800, 990);
    EXPECT_ON(s, TANK_FAULT);
}

static void test_tank_fault_boiling_point_applies(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 0, 1200, 200);
    EXPECT_ON(s, TANK_FAULT_PROBE);
    step(&s, 1, 1270, 200);         /* during the probe: off at once */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1250, 200);
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1249, 200);         /* released: a new probe (the last one did not end with a full tank) */
    EXPECT_ON(s, TANK_FAULT_PROBE);
}

static void test_tank_fault_probe_threshold(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 1, 1099, 200);         /* 109.9 °C */
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 1, 1100, 200);         /* the collector protection threshold: at once */
    EXPECT_ON(s, TANK_FAULT_PROBE);

    s = sim_running();              /* no minimum off time for the probe */
    step(&s, 1, 430, 400);
    EXPECT_OFF(s, DIFF);
    s.tank_fault = true;
    step(&s, 1, 1100, 400);
    EXPECT_ON(s, TANK_FAULT_PROBE);

    s = sim_tank_fault();           /* the threshold is capped at 115 °C, as the protection */
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 1600, .test_s = 300 };
    step(&s, 1, 1149, 200);
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 1, 1150, 200);
    EXPECT_ON(s, TANK_FAULT_PROBE);
}

static void test_tank_fault_probe_tank_takes_heat(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 0, 1100, 200);
    EXPECT_ON(s, TANK_FAULT_PROBE);
    step(&s, 1, 1000, 200);         /* above 95 °C: the probe runs for the minimum run time */
    EXPECT_ON(s, TANK_FAULT_PROBE);
    step(&s, 178, 929, 200);        /* 179 s */
    EXPECT_ON(s, TANK_FAULT_PROBE);
    step(&s, 1, 929, 200);          /* 180 s, 92.9 °C: the tank has room, charging goes on */
    EXPECT_ON(s, TANK_FAULT);
    step(&s, 600, 949, 200);
    EXPECT_ON(s, TANK_FAULT);
    step(&s, 1, 950, 200);          /* by the same rules */
    EXPECT_OFF(s, TANK_FAULT_HOT);
}

static void test_tank_fault_probe_tank_full_5_h(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 0, 1100, 200);
    step(&s, 180, 930, 200);        /* exactly 93.0 °C at the end: not below */
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 1, 1100, 200);
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 5 * HOUR - 2, 1100, 200);      /* 5 h - 1 s after the end of the probe */
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 1, 1100, 200);                 /* exactly 5 h */
    EXPECT_ON(s, TANK_FAULT_PROBE);
}

static void test_tank_fault_charging_resumes_during_the_5_h(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 0, 1100, 200);
    step(&s, 180, 1000, 200);
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, HOUR, 929, 200);       /* the collector cooled below 93 °C: the rules of charging */
    EXPECT_ON(s, TANK_FAULT);
    step(&s, 600, 950, 200);
    EXPECT_OFF(s, TANK_FAULT_HOT);
    step(&s, 600, 1100, 200);       /* but no probe yet */
    EXPECT_OFF(s, TANK_FAULT_HOT);
}

static void test_tank_fault_takes_over_collector_protection(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1120, 800);
    EXPECT_ON(s, COL_PROTECT);
    s.tank_fault = true;
    step(&s, 10, 1120, 800);        /* no off-on of the relay: the run goes on as the probe */
    EXPECT_ON(s, TANK_FAULT_PROBE);
    step(&s, 179, 1000, 800);       /* its time counts from here */
    EXPECT_ON(s, TANK_FAULT_PROBE);
    step(&s, 1, 1000, 800);
    EXPECT_OFF(s, TANK_FAULT_HOT);
}

static void test_tank_fault_recovery(void)
{
    sim_t s = sim_tank_fault();
    step(&s, 0, 800, 200);
    EXPECT_ON(s, TANK_FAULT);
    s.tank_fault = false;
    step(&s, 60, 800, 600);         /* valid again, enough difference: the pump just keeps running */
    EXPECT_ON(s, DIFF);
    step(&s, 1, 630, 600);          /* 3 K, 61 s of run: the minimum run time counts from the start */
    EXPECT_ON(s, MIN_RUN);
    step(&s, 118, 630, 600);
    EXPECT_ON(s, MIN_RUN);
    step(&s, 1, 630, 600);
    EXPECT_OFF(s, DIFF);

    s = sim_tank_fault();           /* stopped at 95 °C on the collector, then valid again */
    step(&s, 0, 900, 200);
    step(&s, 300, 950, 200);
    EXPECT_OFF(s, TANK_FAULT_HOT);
    s.tank_fault = false;
    step(&s, 10, 950, 600);         /* enough difference, but stopped 10 s ago */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 110, 950, 600);
    EXPECT_ON(s, DIFF);

    s = sim_tank_fault();           /* the limits of the tank count again at once */
    step(&s, 0, 900, 200);
    s.tank_fault = false;
    step(&s, 10, 900, 700);
    EXPECT_OFF(s, TANK_MAX);
}

/* -- collector sensor faulty -- */

#define PROBE   SH_PUMP_CF_WINDOW_S     /* 10 min */

/* Collector sensor faulty, pump off for a long time: the first probe starts at once. */
static sim_t sim_col_fault(int16_t t_tank)
{
    sim_t s = sim_off();
    s.col_fault = true;
    step(&s, 1, 500, t_tank);
    EXPECT_ON(s, COL_FAULT_PROBE);
    return s;
}

static void test_col_fault_first_probe(void)
{
    sim_t s = sim_running();        /* stopped 10 s before the sensor is declared faulty */
    step(&s, 1, 430, 400);
    EXPECT_OFF(s, DIFF);
    s.col_fault = true;
    step(&s, 10, 430, 400);
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 109, 430, 400);        /* 119 s after the stop */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 1, 430, 400);
    EXPECT_ON(s, COL_FAULT_PROBE);
}

static void test_col_fault_probe_10_min(void)
{
    sim_t s = sim_col_fault(400);
    step(&s, 1, 500, 500);          /* whatever the tank does meanwhile */
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, PROBE - 2, 500, 404);  /* 10 min - 1 s */
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, 1, 500, 404);          /* 10 min, + 0.4 K: no rise */
    EXPECT_OFF(s, COL_FAULT_WAIT);

    s = sim_col_fault(400);
    step(&s, PROBE, 500, 405);      /* + 0.5 K: there is sun, the pump goes on */
    EXPECT_ON(s, COL_FAULT_CHARGE);

    s = sim_col_fault(400);
    step(&s, PROBE, 500, 380);      /* the tank fell */
    EXPECT_OFF(s, COL_FAULT_WAIT);
}

static void test_col_fault_check_every_10_min(void)
{
    sim_t s = sim_col_fault(400);
    step(&s, PROBE, 500, 420);
    EXPECT_ON(s, COL_FAULT_CHARGE);
    step(&s, PROBE - 1, 500, 420);  /* no rise, but the window is not over */
    EXPECT_ON(s, COL_FAULT_CHARGE);
    step(&s, 1, 500, 425);          /* + 0.5 K in the last 10 min */
    EXPECT_ON(s, COL_FAULT_CHARGE);
    step(&s, PROBE, 500, 429);      /* + 0.4 K in the last 10 min (+ 2.9 K since the start) */
    EXPECT_OFF(s, COL_FAULT_WAIT);
}

static void test_col_fault_wait_doubles(void)
{
    sim_t s = sim_col_fault(400);
    static const uint32_t wait_min[] = { 30, 60, 120, 120 };
    for (int i = 0; i < 4; i++) {
        step(&s, PROBE, 500, 400);                  /* failed probe */
        EXPECT_OFF(s, COL_FAULT_WAIT);
        step(&s, wait_min[i] * 60 - 1, 500, 400);
        EXPECT_OFF(s, COL_FAULT_WAIT);
        step(&s, 1, 500, 400);
        EXPECT_ON(s, COL_FAULT_PROBE);
    }

    step(&s, PROBE, 500, 410);      /* a probe with a rise: back to 30 min */
    EXPECT_ON(s, COL_FAULT_CHARGE);
    step(&s, PROBE, 500, 410);      /* the charging ends */
    EXPECT_OFF(s, COL_FAULT_WAIT);
    step(&s, 30 * 60 - 1, 500, 410);
    EXPECT_OFF(s, COL_FAULT_WAIT);
    step(&s, 1, 500, 410);
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, PROBE, 500, 410);      /* the first failed probe after it: 30 min again */
    EXPECT_OFF(s, COL_FAULT_WAIT);
    step(&s, 30 * 60, 500, 410);
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, PROBE, 500, 410);      /* the second: 60 min */
    step(&s, 60 * 60 - 1, 500, 410);
    EXPECT_OFF(s, COL_FAULT_WAIT);
    step(&s, 1, 500, 410);
    EXPECT_ON(s, COL_FAULT_PROBE);
}

static void test_col_fault_tank_ceiling_5_h(void)
{
    sim_t s = sim_col_fault(900);
    step(&s, 60, 500, 949);
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, 1, 500, 950);          /* exactly 95.0 °C, 61 s into the probe: off at once */
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 60, 500, 930);         /* exactly 93.0 °C: the ceiling still holds */
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 60, 500, 929);         /* released, but no probe for 5 h after the stop */
    EXPECT_OFF(s, COL_FAULT_LOCK);
    step(&s, 5 * HOUR - 121, 500, 800);     /* 5 h - 1 s */
    EXPECT_OFF(s, COL_FAULT_LOCK);
    step(&s, 1, 500, 800);
    EXPECT_ON(s, COL_FAULT_PROBE);

    s = sim_col_fault(900);         /* also while the tank keeps rising */
    step(&s, PROBE, 500, 920);
    EXPECT_ON(s, COL_FAULT_CHARGE);
    step(&s, 300, 500, 950);
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 5 * HOUR - 1, 500, 800);
    EXPECT_OFF(s, COL_FAULT_LOCK);
    step(&s, 1, 500, 800);
    EXPECT_ON(s, COL_FAULT_PROBE);
}

static void test_col_fault_tank_ceiling_while_stopped(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1000, 950);
    EXPECT_OFF(s, TANK_CEILING);
    s.col_fault = true;             /* the 5 h count from the moment the mode sees the ceiling */
    step(&s, 100, 1000, 950);
    EXPECT_OFF(s, TANK_CEILING);
    step(&s, 5 * HOUR - 1, 1000, 900);
    EXPECT_OFF(s, COL_FAULT_LOCK);
    step(&s, 1, 1000, 900);
    EXPECT_ON(s, COL_FAULT_PROBE);
}

static void test_col_fault_tmax_not_used(void)
{
    sim_t s = sim_off();
    step(&s, 0, 900, 750);
    EXPECT_OFF(s, TANK_MAX);
    s.col_fault = true;
    step(&s, 1, 900, 750);
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, PROBE, 900, 760);
    EXPECT_ON(s, COL_FAULT_CHARGE);
}

static void test_col_fault_old_boiling_point_not_used(void)
{
    sim_t s = sim_off();
    step(&s, 0, 1270, 400);
    EXPECT_OFF(s, COL_BOILING);
    s.col_fault = true;             /* the limit cannot be checked any more */
    step(&s, 1, 1270, 400);
    EXPECT_ON(s, COL_FAULT_PROBE);
}

static void test_col_fault_running_pump_goes_on_as_probe(void)
{
    sim_t s = sim_running();        /* running for a long time, tank 40.0 */
    s.col_fault = true;
    step(&s, 1, 500, 410);
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, PROBE - 1, 500, 414);  /* judged 10 min after the fault, against the tank at that moment */
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, 1, 500, 414);
    EXPECT_OFF(s, COL_FAULT_WAIT);

    s = sim_running();
    s.col_fault = true;
    step(&s, 1, 500, 410);
    step(&s, PROBE, 500, 415);
    EXPECT_ON(s, COL_FAULT_CHARGE);
}

static void test_col_fault_heater(void)
{
    /* Failed probe, then the tank rises 0.5 K in the last 10 min of the wait: a heater. */
    sim_t s = sim_col_fault(400);
    step(&s, PROBE, 500, 400);
    EXPECT_OFF(s, COL_FAULT_WAIT);
    run(&s, 30 * 60 - 1, 500, 400);
    EXPECT_OFF(s, COL_FAULT_WAIT);
    step(&s, 1, 500, 405);          /* the probe is due */
    EXPECT_OFF(s, HEATER);
    run(&s, 300, 500, 405);
    EXPECT_OFF(s, HEATER);
    run(&s, 300, 500, 409);         /* + 0.4 K in the last 10 min: the rise has stopped */
    EXPECT_ON(s, COL_FAULT_PROBE);

    s = sim_col_fault(400);         /* 0.4 K: not a heater */
    step(&s, PROBE, 500, 400);
    run(&s, 30 * 60 - 1, 500, 400);
    step(&s, 1, 500, 404);
    EXPECT_ON(s, COL_FAULT_PROBE);
}

static void test_col_fault_heater_needs_10_min_of_stopped_pump(void)
{
    sim_t s = sim_running();
    step(&s, 1, 430, 400);          /* the pump stops, tank 40.0 */
    EXPECT_OFF(s, DIFF);
    s.col_fault = true;
    step(&s, PROBE - 1, 430, 410);  /* 10 min - 1 s later, + 1 K: nothing to compare with yet */
    EXPECT_ON(s, COL_FAULT_PROBE);

    s = sim_running();
    step(&s, 1, 430, 400);
    s.col_fault = true;
    step(&s, PROBE, 430, 410);      /* exactly 10 min */
    EXPECT_OFF(s, HEATER);
}

static void test_col_fault_recovery(void)
{
    sim_t s = sim_col_fault(400);
    s.col_fault = false;
    step(&s, 60, 500, 400);         /* valid again during the probe: the normal rules, pump as it is */
    EXPECT_ON(s, DIFF);

    s = sim_col_fault(400);
    s.col_fault = false;
    step(&s, 60, 420, 400);         /* no difference: stops after the minimum run time, not at once */
    EXPECT_ON(s, MIN_RUN);
    step(&s, 119, 420, 400);
    EXPECT_ON(s, MIN_RUN);
    step(&s, 1, 420, 400);
    EXPECT_OFF(s, DIFF);

    s = sim_col_fault(400);
    step(&s, PROBE, 500, 400);
    EXPECT_OFF(s, COL_FAULT_WAIT);
    s.col_fault = false;
    step(&s, 10, 500, 400);         /* enough difference, stopped 10 s ago */
    EXPECT_OFF(s, MIN_OFF);
    step(&s, 110, 500, 400);
    EXPECT_ON(s, DIFF);
}

static void test_col_fault_again_does_not_bring_a_probe_earlier(void)
{
    sim_t s = sim_col_fault(400);
    step(&s, PROBE, 500, 400);
    EXPECT_OFF(s, COL_FAULT_WAIT);
    s.col_fault = false;
    step(&s, 300, 410, 400);        /* valid for a while, nothing to do */
    EXPECT_OFF(s, DIFF);
    s.col_fault = true;
    step(&s, 300, 410, 400);        /* faulty again 10 min after the failed probe */
    EXPECT_OFF(s, COL_FAULT_WAIT);
    step(&s, 20 * 60 - 1, 410, 400);
    EXPECT_OFF(s, COL_FAULT_WAIT);
    step(&s, 1, 410, 400);
    EXPECT_ON(s, COL_FAULT_PROBE);

    s = sim_col_fault(900);         /* the 5 h after the ceiling as well */
    step(&s, 60, 500, 950);
    EXPECT_OFF(s, TANK_CEILING);
    s.col_fault = false;
    step(&s, HOUR, 800, 900);
    EXPECT_OFF(s, TANK_MAX);
    s.col_fault = true;
    step(&s, HOUR, 800, 900);
    EXPECT_OFF(s, COL_FAULT_LOCK);
    step(&s, 3 * HOUR - 1, 800, 900);
    EXPECT_OFF(s, COL_FAULT_LOCK);
    step(&s, 1, 800, 900);
    EXPECT_ON(s, COL_FAULT_PROBE);
}

static void test_fault_test_during_fault_modes(void)
{
    sim_t s = sim_col_fault(400);   /* the test takes over a probe; after it a new probe is due */
    step(&s, 60, 500, 400);
    key_plus(&s);
    EXPECT_ON(s, TEST);
    step(&s, 300, 500, 400);
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, PROBE - 1, 500, 410);
    EXPECT_ON(s, COL_FAULT_PROBE);
    step(&s, 1, 500, 410);
    EXPECT_ON(s, COL_FAULT_CHARGE);

    s = sim_tank_fault();           /* tank faulty, collector cold: the test runs, then off */
    step(&s, 0, 300, 200);
    EXPECT_OFF(s, TANK_FAULT);
    key_plus(&s);
    EXPECT_ON(s, TEST);
    step(&s, 300, 300, 200);
    EXPECT_OFF(s, TANK_FAULT);
}

int main(void)
{
    test_initial_state();

    test_diff_on_threshold();
    test_diff_off_threshold();
    test_diff_between_thresholds_keeps_state();
    test_diff_negative_temperatures();

    test_min_run_time();
    test_min_run_time_difference_recovers();

    test_min_off_time();
    test_min_off_time_counts_from_start();
    test_min_off_time_zero_disables_it();

    test_col_min_blocks_start();
    test_col_min_no_return_zone();
    test_col_min_does_not_stop_running_pump();

    test_tank_max_threshold();
    test_tank_max_stops_at_once();
    test_tank_max_return_zone();
    test_tank_max_restart_waits_min_off_time();

    test_tank_ceiling_threshold();
    test_tank_ceiling_return_zone();
    test_tank_ceiling_wins_over_tank_max();

    test_col_protect_threshold();
    test_col_protect_ignores_min_off_time();
    test_col_protect_return_zone_and_min_run();
    test_col_protect_zone_does_not_start();
    test_col_protect_hands_over_to_difference();
    test_col_protect_takes_over_running_pump();
    test_col_protect_stops_at_tank_ceiling();
    test_col_protect_cap();
    test_col_protect_below_cap_is_used_as_set();

    test_col_boil_threshold();
    test_col_boil_return_zone();
    test_col_boil_stops_running_pump();
    test_col_boil_before_tank_ceiling();

    test_sensor_invalid_no_start();
    test_sensor_invalid_stops_at_once();
    test_sensor_recovery();
    test_sensor_invalid_keeps_limits();
    test_sensor_invalid_limits_follow_valid_sensor();

    test_priority_order();
    test_min_times_only_for_difference();
    test_reason_names();


    test_test_starts_at_once();
    test_test_runs_for_the_set_time();
    test_test_ignores_the_other_rules();
    test_test_refused_by_the_two_limits();
    test_test_stopped_by_the_two_limits();
    test_test_limit_of_a_faulty_sensor_cannot_be_checked();
    test_test_plus_adds_a_minute();
    test_test_minus_takes_a_minute();
    test_test_menu_ends_it();
    test_test_runs_at_least_a_minute();
    test_test_end_pump_stays_on_by_a_rule();
    test_test_end_off_time_counts_from_the_end();
    test_test_takes_over_a_running_pump();
    test_seize_after_24_h();
    test_seize_waits_for_collector_not_colder();
    test_seize_after_12_h_more_whatever_the_temperatures();
    test_seize_any_run_restarts_the_24_h();
    test_seize_ignores_the_other_rules();
    test_seize_refused_and_stopped_by_the_two_limits();
    test_seize_end_as_the_end_of_the_test();
    test_seize_taken_over_by_the_test();
    test_fault_both_sensors();
    test_fault_sensor_without_a_value();
    test_tank_fault_start_threshold();
    test_tank_fault_min_off_time();
    test_tank_fault_stop_threshold_and_min_run();
    test_tank_fault_collector_95();
    test_tank_fault_restart_after_95_waits_min_off_time();
    test_tank_fault_tmax_and_ceiling_not_used();
    test_tank_fault_boiling_point_applies();
    test_tank_fault_probe_threshold();
    test_tank_fault_probe_tank_takes_heat();
    test_tank_fault_probe_tank_full_5_h();
    test_tank_fault_charging_resumes_during_the_5_h();
    test_tank_fault_takes_over_collector_protection();
    test_tank_fault_recovery();
    test_col_fault_first_probe();
    test_col_fault_probe_10_min();
    test_col_fault_check_every_10_min();
    test_col_fault_wait_doubles();
    test_col_fault_tank_ceiling_5_h();
    test_col_fault_tank_ceiling_while_stopped();
    test_col_fault_tmax_not_used();
    test_col_fault_old_boiling_point_not_used();
    test_col_fault_running_pump_goes_on_as_probe();
    test_col_fault_heater();
    test_col_fault_heater_needs_10_min_of_stopped_pump();
    test_col_fault_recovery();
    test_col_fault_again_does_not_bring_a_probe_earlier();
    test_fault_test_during_fault_modes();

    return test_result("pump logic");
}
