/* Host test of the pump decision (main/sh_pump_logic.c) — no ESP-IDF needed.
 *
 *   make -C project/solar-heater-hap/test
 *
 * Every rule of the normal mode (doc/operating-modes.md, 2.1; doc/tasks/step1-control-loop.md,
 * 3.2) has its own test: exactly at the threshold, 0.1 K on both sides, the minimum times and
 * the priorities. Temperatures are tenths of °C, times are seconds. */

#include <stdio.h>
#include <string.h>
#include "sh_state.h"
#include "sh_pump_logic.h"

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

#define LONG_AGO    100000u     /* s: every minimum time is over */

static int s_checks, s_failed;

/* The pump with its clock, as the control task keeps them. */
typedef struct {
    const sh_pump_params_t *p;
    sh_pump_state_t st;
    uint32_t elapsed_s;         /* since the pump last changed */
} sim_t;

/* Pump off, nothing latched, minimum off time over. */
static sim_t sim_off(void)
{
    sim_t s = { .p = &P, .elapsed_s = LONG_AGO };
    return s;
}

/* `dt_s` seconds later the temperatures are `t_col` / `t_tank`: one decision. */
static void step(sim_t *s, uint32_t dt_s, int16_t t_col, int16_t t_tank)
{
    bool was_on = s->st.on;
    s->elapsed_s += dt_s;
    sh_pump_decide(s->p, &s->st, t_col, t_tank, s->elapsed_s);
    if (s->st.on != was_on) {
        s->elapsed_s = 0;
    }
}

static void expect(const char *file, int line, const sim_t *s, bool on, sh_pump_reason_t reason)
{
    s_checks++;
    if (s->st.on != on || s->st.reason != reason) {
        s_failed++;
        printf("FAIL %s:%d: pump %s (%s), expected %s (%s)\n", file, line,
               s->st.on ? "ON" : "OFF", sh_pump_reason_name(s->st.reason),
               on ? "ON" : "OFF", sh_pump_reason_name(reason));
    }
}

#define CHECK_EQ_PUMP(a, b)                                                            \
    do {                                                                                \
        s_checks++;                                                                     \
        if ((long)(a) != (long)(b)) {                                                   \
            s_failed++;                                                                 \
            printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #a, (long)(a), (long)(b)); \
        }                                                                               \
    } while (0)

#define EXPECT_ON(s, reason)    expect(__FILE__, __LINE__, &(s), true, SH_PUMP_REASON_##reason)
#define EXPECT_OFF(s, reason)   expect(__FILE__, __LINE__, &(s), false, SH_PUMP_REASON_##reason)

/* Pump running by the difference (collector 50.0, tank 40.0), minimum run time over. */
static sim_t sim_running(void)
{
    sim_t s = sim_off();
    step(&s, 0, 500, 400);
    EXPECT_ON(s, DIFF);
    s.elapsed_s = LONG_AGO;
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
                               .tank_max = 700, .col_min = -300, .col_protect = 1100,
                               .col_protect_cap = 1150, .col_boiling = 1270 };
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
                               .tank_max = 700, .col_min = 250, .col_protect = 1100,
                               .col_protect_cap = 1150, .col_boiling = 1270 };
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
                               .tank_max = 700, .col_min = 250, .col_protect = 1600,
                               .col_protect_cap = 1150, .col_boiling = 1270 };
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
                               .tank_max = 700, .col_min = 250, .col_protect = 900,
                               .col_protect_cap = 1150, .col_boiling = 1270 };
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

/* ---- Frost protection ------------------------------------------------------------------ */

/* Water: threshold 4.0 °C, frost protection on */
static const sh_pump_params_t F = {
    .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
    .tank_max = 700, .col_min = 250, .col_protect = 1100,
    .col_protect_cap = 1150, .col_boiling = 1270, .frost_on = true, .frost = 40,
};

static sim_t sim_frost_off(void)
{
    sim_t s = { .p = &F, .elapsed_s = LONG_AGO };
    return s;
}

static void test_frost_threshold(void)
{
    sim_t s = sim_frost_off();
    step(&s, 1, 40, 100);           /* exactly 4.0 °C: not below */
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 39, 100);           /* 3.9 °C */
    EXPECT_ON(s, FROST);

    /* a threshold below zero (propylene glycol) */
    s = sim_frost_off();
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 1100,
                               .col_protect_cap = 1150, .col_boiling = 1270, .frost_on = true,
                               .frost = -70 };
    step(&s, 1, -70, 100);
    EXPECT_OFF(s, DIFF);
    step(&s, 1, -71, 100);
    EXPECT_ON(s, FROST);
}

static void test_frost_ignores_min_off_time(void)
{
    sim_t s = { .p = &F };          /* just stopped */
    step(&s, 0, 39, 100);
    EXPECT_ON(s, FROST);
}

static void test_frost_needs_tank_5(void)
{
    sim_t s = sim_frost_off();
    step(&s, 1, 20, 49);            /* tank 4.9 °C: no heat to give */
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 20, 50);            /* exactly 5.0 °C */
    EXPECT_ON(s, FROST);
}

static void test_frost_stop_zone_and_min_run(void)
{
    sim_t s = sim_frost_off();
    step(&s, 0, 39, 100);
    EXPECT_ON(s, FROST);
    step(&s, 179, 60, 100);         /* threshold + 2 K, but the minimum run time is not over */
    EXPECT_ON(s, FROST);
    step(&s, 1, 59, 100);           /* 180 s, but 0.1 K below the threshold + 2 K */
    EXPECT_ON(s, FROST);
    step(&s, 1, 60, 100);           /* exactly the threshold + 2 K, after the minimum run time */
    EXPECT_OFF(s, DIFF);
    step(&s, LONG_AGO, 59, 100);    /* in the zone: a stopped pump stays stopped */
    EXPECT_OFF(s, DIFF);
    step(&s, 1, 39, 100);
    EXPECT_ON(s, FROST);
}

static void test_frost_tank_below_5_stops_at_once(void)
{
    sim_t s = sim_frost_off();
    step(&s, 0, 20, 60);
    EXPECT_ON(s, FROST);
    step(&s, 1, 20, 50);            /* still 5.0 °C */
    EXPECT_ON(s, FROST);
    step(&s, 1, 20, 49);            /* 4.9 °C, 2 s after the start: no minimum run time */
    EXPECT_OFF(s, DIFF);
    s_checks++;
    if (s.st.frost) {
        s_failed++;
        printf("FAIL: the frost protection is still latched\n");
    }
}

static void test_frost_switch(void)
{
    /* switched off: no start */
    sim_t s = sim_off();            /* P: frost protection off */
    step(&s, 1, -100, 100);
    EXPECT_OFF(s, DIFF);

    /* switched off while it runs: ends at once */
    s = sim_frost_off();
    step(&s, 0, 20, 100);
    EXPECT_ON(s, FROST);
    s.p = &P;
    step(&s, 1, 20, 100);
    EXPECT_OFF(s, DIFF);
}

static void test_frost_priority(void)
{
    /* tank ceiling > frost protection */
    sim_t s = sim_frost_off();
    step(&s, 1, 20, 950);
    EXPECT_OFF(s, TANK_CEILING);

    /* running frost protection is stopped by the tank ceiling at once */
    s = sim_frost_off();
    step(&s, 0, 20, 940);
    EXPECT_ON(s, FROST);
    step(&s, 1, 20, 950);
    EXPECT_OFF(s, TANK_CEILING);

    /* frost protection > Tmax (and the difference: the collector is far below the tank) */
    s = sim_frost_off();
    step(&s, 1, 20, 720);
    EXPECT_ON(s, FROST);

    /* collector protection > frost protection (thresholds that overlap only in a test) */
    s = sim_frost_off();
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 30,
                               .col_protect_cap = 1150, .col_boiling = 1270, .frost_on = true,
                               .frost = 40 };
    step(&s, 1, 35, 100);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 29, 100);           /* below the collector protection, still within its zone */
    EXPECT_ON(s, COL_PROTECT);

    /* boiling point > frost protection (likewise) */
    s = sim_frost_off();
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 1100,
                               .col_protect_cap = 1150, .col_boiling = 30, .frost_on = true,
                               .frost = 40 };
    step(&s, 1, 35, 100);
    EXPECT_OFF(s, COL_BOILING);
}

static void test_frost_sensor_invalid(void)
{
    sim_t s = sim_frost_off();
    step(&s, 0, 20, 100);
    EXPECT_ON(s, FROST);
    step(&s, 1, 20, SH_TEMP_INVALID);
    EXPECT_OFF(s, SENSOR);
    s = sim_frost_off();
    step(&s, 1, SH_TEMP_INVALID, 100);
    EXPECT_OFF(s, SENSOR);
}

static void test_frost_takes_over_from_difference(void)
{
    /* a threshold of 10 °C: the pump runs by the difference, then the collector falls below it */
    sim_t s = sim_frost_off();
    s.p = &(sh_pump_params_t){ .diff_on = 30, .diff_off = 10, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 100, .col_protect = 1100,
                               .col_protect_cap = 1150, .col_boiling = 1270, .frost_on = true,
                               .frost = 100 };
    step(&s, 1, 130, 60);
    EXPECT_ON(s, DIFF);
    step(&s, 1, 99, 60);
    EXPECT_ON(s, FROST);
    step(&s, LONG_AGO, 120, 60);    /* threshold + 2 K: back to the difference (6 K > 1 K) */
    EXPECT_ON(s, DIFF);
}

/* ---- Computed limits in the parameters ------------------------------------------------- */

static void test_limits_from_params(void)
{
    /* ~9 % glycol, 2.2 bar, collector maximum 150 °C: boiling 136.5 °C, cap 126.5 °C */
    static const sh_pump_params_t G = {
        .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
        .tank_max = 700, .col_min = 250, .col_protect = 1600,
        .col_protect_cap = 1265, .col_boiling = 1365,
    };
    sim_t s = sim_off();
    s.p = &G;
    step(&s, 1, 1264, 800);         /* the setting 160 °C is limited to the cap */
    EXPECT_OFF(s, TANK_MAX);
    step(&s, 1, 1265, 800);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1364, 800);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 1365, 800);         /* the boiling point stops the running pump */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1345, 800);         /* exactly boiling - 2 K: still forbidden */
    EXPECT_OFF(s, COL_BOILING);
    step(&s, 1, 1344, 800);
    EXPECT_ON(s, COL_PROTECT);

    /* new installer data apply at the next decision: the boiling point falls to 127.7 °C */
    s.p = &(sh_pump_params_t){ .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
                               .tank_max = 700, .col_min = 250, .col_protect = 1100,
                               .col_protect_cap = 1150, .col_boiling = 1277 };
    step(&s, 1, 1344, 800);
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
    s_checks++;
    if (s.st.on || s.st.reason != SH_PUMP_REASON_NONE) {
        s_failed++;
        printf("FAIL: the zero state is not \"pump off, no decision\"\n");
    }
}

/* ==== Group C: night cooling session, vacation (doc/tasks/step10-cooling-vacation-seasons.md 3.2,
 * 3.3) ===================================================================================== */

static sh_pump_params_t s_pc;

/* A cooling session (sh_cool decided it), pump off, minimum off time over. */
static sim_t sim_cool(void)
{
    s_pc = P;
    s_pc.cool = true;
    sim_t s = sim_off();
    s.p = &s_pc;
    return s;
}

static void test_cool_reverse_difference(void)
{
    /* starts at tank - collector >= the switch-on difference (6 K), exactly at it, 0.1 K below not */
    sim_t s = sim_cool();
    step(&s, 1, 641, 700);                  /* 5.9 K */
    EXPECT_OFF(s, COOL_WAIT);
    step(&s, 1, 640, 700);                  /* 6.0 K */
    EXPECT_ON(s, COOL);
    CHECK_EQ_PUMP(s.st.cool, true);
    /* runs while above the switch-off difference; at it, after the minimum run time, it stops */
    step(&s, 179, 660, 700);                /* 4.0 K, 3 min not over */
    EXPECT_ON(s, COOL);
    step(&s, 1, 660, 700);
    EXPECT_OFF(s, COOL_WAIT);
    CHECK_EQ_PUMP(s.st.cool, false);
    /* a new start: the difference again, after the minimum off time */
    step(&s, 60, 630, 700);
    EXPECT_OFF(s, COOL_WAIT);
    step(&s, 59, 630, 700);
    EXPECT_OFF(s, COOL_WAIT);
    step(&s, 1, 630, 700);                  /* 2 min */
    EXPECT_ON(s, COOL);
    step(&s, 180, 659, 700);                /* 4.1 K: still above the switch-off difference */
    EXPECT_ON(s, COOL);
}

static void test_cool_not_by_normal_rules(void)
{
    /* during the session: no Tmax, no normal charging, no minimum collector temperature */
    sim_t s = sim_cool();
    step(&s, 1, 100, 800);                  /* collector below its minimum, tank above Tmax */
    EXPECT_ON(s, COOL);
    s = sim_cool();
    step(&s, 1, 500, 400);                  /* collector warmer: no charging in the session */
    EXPECT_OFF(s, COOL_WAIT);
}

static void test_cool_session_end_stops_at_once(void)
{
    sim_t s = sim_cool();
    step(&s, 1, 600, 700);
    EXPECT_ON(s, COOL);
    s_pc.cool = false;                      /* the target, the sunrise ...: at once, no minimum run */
    step(&s, 10, 600, 650);
    EXPECT_OFF(s, DIFF);
    CHECK_EQ_PUMP(s.st.cool, false);
    /* a pump the cooling did not run is left to the normal rules */
    s = sim_running();
    s_pc = P;
    s.p = &s_pc;
    step(&s, 1, 500, 400);
    EXPECT_ON(s, DIFF);
}

static void test_cool_below_protections(void)
{
    /* the collector protection, the frost protection, the boiling point and the ceiling come first */
    sim_t s = sim_cool();
    step(&s, 1, 1150, 900);
    EXPECT_ON(s, COL_PROTECT);
    s = sim_cool();
    step(&s, 1, 600, 950);
    EXPECT_OFF(s, TANK_CEILING);
    s = sim_cool();
    s_pc.frost_on = true;
    step(&s, 1, 30, 600);
    EXPECT_ON(s, FROST);
    CHECK_EQ_PUMP(s.st.cool, false);
    s = sim_cool();
    step(&s, 1, 1280, 700);
    EXPECT_OFF(s, COL_BOILING);
    s = sim_cool();
    step(&s, 1, SH_TEMP_INVALID, 700);
    EXPECT_OFF(s, SENSOR);
}

static void test_vacation_day(void)
{
    /* normal charging off at once */
    s_pc = P;
    s_pc.vacation = true;
    s_pc.vac_protect = 900;                 /* 75 % of a 120 °C collector */
    sim_t s = sim_running();
    s.p = &s_pc;
    step(&s, 1, 500, 400);
    EXPECT_OFF(s, VACATION);
    s = sim_off();
    s.p = &s_pc;
    step(&s, 1, 800, 300);
    EXPECT_OFF(s, VACATION);
    /* the collector protection at its own threshold, also below the normal one; 2 K return zone
     * after the minimum run time */
    step(&s, 1, 899, 300);
    EXPECT_OFF(s, VACATION);
    step(&s, 1, 900, 300);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 879, 300);                  /* below 88 °C, but the minimum run time */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 178, 879, 300);
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 880, 300);                  /* 88.0: not below the zone */
    EXPECT_ON(s, COL_PROTECT);
    step(&s, 1, 879, 300);
    EXPECT_OFF(s, VACATION);
    /* the ceiling still applies */
    step(&s, 1, 1000, 950);
    EXPECT_OFF(s, TANK_CEILING);
    CHECK_EQ_PUMP(sh_pump_col_protect(&s_pc), 900);
    s_pc.vac_protect = 1300;                /* the threshold of the vacation is not capped by the normal cap */
    CHECK_EQ_PUMP(sh_pump_col_protect(&s_pc), 1300);
    s_pc.vacation = false;
    CHECK_EQ_PUMP(sh_pump_col_protect(&s_pc), 1100);
}

static void test_vacation_night(void)
{
    /* the vacation night is a cooling session: the reverse difference */
    s_pc = P;
    s_pc.vacation = true;
    s_pc.vac_protect = 900;
    s_pc.cool = true;
    sim_t s = sim_off();
    s.p = &s_pc;
    step(&s, 1, 300, 360);
    EXPECT_ON(s, COOL);
    step(&s, 180, 320, 359);
    EXPECT_OFF(s, COOL_WAIT);               /* 3.9 K */
}

static void test_reason_names(void)
{
    for (int r = 0; r < SH_PUMP_REASON_COUNT; r++) {
        s_checks++;
        if (strcmp(sh_pump_reason_name((sh_pump_reason_t)r), "?") == 0) {
            s_failed++;
            printf("FAIL: reason %d has no name\n", r);
        }
    }
    s_checks++;
    if (strcmp(sh_pump_reason_name(SH_PUMP_REASON_COUNT), "?") != 0) {
        s_failed++;
        printf("FAIL: a reason out of range has a name\n");
    }
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

    test_frost_threshold();
    test_frost_ignores_min_off_time();
    test_frost_needs_tank_5();
    test_frost_stop_zone_and_min_run();
    test_frost_tank_below_5_stops_at_once();
    test_frost_switch();
    test_frost_priority();
    test_frost_sensor_invalid();
    test_frost_takes_over_from_difference();
    test_limits_from_params();

    test_priority_order();
    test_min_times_only_for_difference();
    test_cool_reverse_difference();
    test_cool_not_by_normal_rules();
    test_cool_session_end_stops_at_once();
    test_cool_below_protections();
    test_vacation_day();
    test_vacation_night();

    test_reason_names();

    printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
