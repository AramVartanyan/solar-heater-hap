/* Host test of sh_pump_decide() (main/sh_pump_logic.c): every rule of the normal mode and the
 * edge cases — exactly at a threshold, ±0.1 K, the minimum times, the priorities.
 * Run: make -C project/solar-heater-hap/test */

#include <stdio.h>
#include <string.h>
#include "sh_pump_logic.h"
#include "sh_state.h"

static int s_checks, s_failed;

#define LONG_S  100000u          /* far beyond any minimum time */
#define INV     SH_TEMP_INVALID

/* defaults of doc/tasks/step1-control-loop.md 3.2 */
static const sh_pump_params_t P = {
    .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120,
    .tank_max = 700, .col_min = 250, .col_protect = 1100,
};

static sh_pump_state_t off_state(void)
{
    sh_pump_state_t st;
    memset(&st, 0, sizeof(st));
    return st;
}

static sh_pump_state_t on_state(sh_pump_reason_t reason)
{
    sh_pump_state_t st = off_state();
    st.on = true;
    st.reason = reason;
    return st;
}

static void check(int line, const char *what, const sh_pump_state_t *st, bool on, sh_pump_reason_t reason)
{
    s_checks++;
    if (st->on != on || st->reason != reason) {
        s_failed++;
        printf("FAIL line %d: %s: got %s (%s), expected %s (%s)\n", line, what, st->on ? "on" : "off",
               sh_pump_reason_str(st->reason), on ? "on" : "off", sh_pump_reason_str(reason));
    }
}

/* one step from a given state, then check */
#define STEP(st, p, col, tank, el, on, reason) do {                 \
        sh_pump_decide((p), (col), (tank), (el), &(st));            \
        check(__LINE__, #col " / " #tank " / " #el, &(st), (on), (reason)); \
    } while (0)

#define FROM_OFF(col, tank, el, on, reason) do { sh_pump_state_t s_ = off_state(); \
        STEP(s_, &P, col, tank, el, on, reason); } while (0)
#define FROM_ON(r0, col, tank, el, on, reason) do { sh_pump_state_t s_ = on_state(r0); \
        STEP(s_, &P, col, tank, el, on, reason); } while (0)

static void test_diff_on(void)
{
    FROM_OFF(500, 440, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);    /* exactly 6.0 K */
    FROM_OFF(501, 440, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);    /* 6.1 K */
    FROM_OFF(499, 440, LONG_S, false, SH_PUMP_REASON_DIFF_LOW);   /* 5.9 K */
    FROM_OFF(400, 500, LONG_S, false, SH_PUMP_REASON_DIFF_LOW);   /* collector colder */
}

static void test_diff_off(void)
{
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 500, 460, LONG_S, false, SH_PUMP_REASON_DIFF_LOW);  /* exactly 4.0 K */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 499, 460, LONG_S, false, SH_PUMP_REASON_DIFF_LOW);  /* 3.9 K */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 501, 460, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);   /* 4.1 K */
    /* between the two thresholds: running stays on, stopped stays off */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 510, 460, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);   /* 5.0 K */
    FROM_OFF(510, 460, LONG_S, false, SH_PUMP_REASON_DIFF_LOW);
}

static void test_min_run(void)
{
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 500, 470, 179, true,  SH_PUMP_REASON_MIN_RUN);      /* 3.0 K, 2:59 */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 500, 470, 180, false, SH_PUMP_REASON_DIFF_LOW);     /* 3:00 */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 500, 470, 0,   true,  SH_PUMP_REASON_MIN_RUN);
    /* collector under the tank: still held by the minimum run time */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 400, 470, 100, true,  SH_PUMP_REASON_MIN_RUN);
    /* run time over while the difference is big: on by the difference */
    FROM_ON(SH_PUMP_REASON_MIN_RUN, 520, 470, 100, true,  SH_PUMP_REASON_DIFF_ON);
}

static void test_min_off(void)
{
    FROM_OFF(600, 400, 119, false, SH_PUMP_REASON_MIN_OFF);       /* 1:59 */
    FROM_OFF(600, 400, 120, true,  SH_PUMP_REASON_DIFF_ON);       /* 2:00 */
    FROM_OFF(600, 400, 0,   false, SH_PUMP_REASON_MIN_OFF);

    sh_pump_params_t p0 = P;                                      /* 0 = no minimum off time */
    p0.min_off_s = 0;
    sh_pump_state_t st = off_state();
    STEP(st, &p0, 600, 400, 0, true, SH_PUMP_REASON_DIFF_ON);
}

static void test_tank_max(void)
{
    FROM_OFF(800, 699, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);    /* 69.9 °C: allowed */
    FROM_OFF(800, 700, LONG_S, false, SH_PUMP_REASON_TANK_MAX);   /* 70.0 °C */
    /* a running pump stops at once at Tmax, the minimum run time does not hold it */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 800, 700, 0, false, SH_PUMP_REASON_TANK_MAX);

    /* return zone: blocked until strictly below Tmax - 2 K */
    sh_pump_state_t st = off_state();
    STEP(st, &P, 800, 700, LONG_S, false, SH_PUMP_REASON_TANK_MAX);
    STEP(st, &P, 800, 690, LONG_S, false, SH_PUMP_REASON_TANK_MAX);
    STEP(st, &P, 800, 680, LONG_S, false, SH_PUMP_REASON_TANK_MAX);   /* exactly 68.0 °C */
    STEP(st, &P, 800, 679, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);    /* 67.9 °C */
    /* the latch is released: 69.0 °C is allowed again */
    STEP(st, &P, 800, 690, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);

    /* back below the limit, the minimum off time still applies to the difference */
    st = off_state();
    STEP(st, &P, 800, 700, 10, false, SH_PUMP_REASON_TANK_MAX);
    STEP(st, &P, 800, 670, 60, false, SH_PUMP_REASON_MIN_OFF);
}

static void test_col_min(void)
{
    FROM_OFF(249, 100, LONG_S, false, SH_PUMP_REASON_COL_COLD);   /* 24.9 °C */
    FROM_OFF(250, 100, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);    /* exactly 25.0 °C */
    /* it does not stop a running pump */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 200, 100, LONG_S, true, SH_PUMP_REASON_DIFF_ON);
    /* the difference is checked first */
    FROM_OFF(200, 180, LONG_S, false, SH_PUMP_REASON_DIFF_LOW);
}

static void test_tank_ceiling(void)
{
    /* stops at once, also the collector protection, whatever the minimum run time */
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1120, 950, 0, false, SH_PUMP_REASON_TANK_CEILING);
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1120, 949, 0, true,  SH_PUMP_REASON_COL_PROTECT);
    FROM_OFF(1120, 949, 0, true, SH_PUMP_REASON_COL_PROTECT);     /* 94.9 °C, never reached 95 */

    sh_pump_state_t st = on_state(SH_PUMP_REASON_COL_PROTECT);
    STEP(st, &P, 1120, 951, 0,      false, SH_PUMP_REASON_TANK_CEILING);
    STEP(st, &P, 1120, 940, LONG_S, false, SH_PUMP_REASON_TANK_CEILING);
    STEP(st, &P, 1120, 930, LONG_S, false, SH_PUMP_REASON_TANK_CEILING);  /* exactly 93.0 °C */
    STEP(st, &P, 1120, 929, 0,      true,  SH_PUMP_REASON_COL_PROTECT);   /* 92.9 °C */
}

static void test_col_protect(void)
{
    /* on at once at the threshold: ignores the minimum off time and Tmax */
    FROM_OFF(1100, 800, 0, true,  SH_PUMP_REASON_COL_PROTECT);    /* exactly 110.0 °C */
    FROM_OFF(1099, 800, 0, false, SH_PUMP_REASON_TANK_MAX);       /* 109.9 °C, tank over Tmax */
    FROM_OFF(1099, 500, 0, false, SH_PUMP_REASON_MIN_OFF);        /* 109.9 °C, normal charging */
    /* a pump running by the difference becomes protection */
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 1100, 500, 10, true, SH_PUMP_REASON_COL_PROTECT);

    /* off below the threshold - 2 K, only after the minimum run time */
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1080, 800, LONG_S, true,  SH_PUMP_REASON_COL_PROTECT); /* 108.0 */
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1079, 800, LONG_S, false, SH_PUMP_REASON_TANK_MAX);    /* 107.9 */
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1079, 800, 179,    true,  SH_PUMP_REASON_COL_PROTECT);
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 900,  800, 179,    true,  SH_PUMP_REASON_COL_PROTECT);
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1079, 800, 180,    false, SH_PUMP_REASON_TANK_MAX);
    /* protection over, tank under Tmax: the difference takes over */
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1079, 500, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1079, 1040, LONG_S, false, SH_PUMP_REASON_TANK_CEILING);

    /* the parameter is capped at 115 °C */
    sh_pump_params_t p = P;
    p.col_protect = 1300;
    sh_pump_state_t st;
    s_checks++;
    if (sh_pump_col_protect(&p) != SH_PUMP_COL_PROTECT_CAP) {
        s_failed++;
        printf("FAIL: protection not capped (%d)\n", sh_pump_col_protect(&p));
    }
    st = off_state();
    STEP(st, &p, 1150, 800, 0, true,  SH_PUMP_REASON_COL_PROTECT);
    st = off_state();
    STEP(st, &p, 1149, 800, 0, false, SH_PUMP_REASON_TANK_MAX);
    /* a lower setting is used as is */
    p.col_protect = 700;
    st = off_state();
    STEP(st, &p, 700, 690, 0, true,  SH_PUMP_REASON_COL_PROTECT);
    st = on_state(SH_PUMP_REASON_COL_PROTECT);
    STEP(st, &p, 679, 690, LONG_S, false, SH_PUMP_REASON_DIFF_LOW);
}

static void test_col_boiling(void)
{
    /* no start for any reason, a running pump stops too */
    FROM_OFF(1270, 500, LONG_S, false, SH_PUMP_REASON_COL_BOILING);          /* exactly 127.0 °C */
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1270, 500, 0, false, SH_PUMP_REASON_COL_BOILING);
    FROM_OFF(1269, 500, 0, true, SH_PUMP_REASON_COL_PROTECT);                /* 126.9 °C */

    sh_pump_state_t st = off_state();
    STEP(st, &P, 1300, 500, LONG_S, false, SH_PUMP_REASON_COL_BOILING);
    STEP(st, &P, 1260, 500, LONG_S, false, SH_PUMP_REASON_COL_BOILING);
    STEP(st, &P, 1250, 500, LONG_S, false, SH_PUMP_REASON_COL_BOILING);      /* exactly 125.0 °C */
    STEP(st, &P, 1249, 500, 0,      true,  SH_PUMP_REASON_COL_PROTECT);      /* 124.9 °C */
}

static void test_priorities(void)
{
    /* boiling before the tank ceiling */
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, 1270, 950, 0, false, SH_PUMP_REASON_COL_BOILING);
    /* tank ceiling before the collector protection */
    FROM_OFF(1200, 950, 0, false, SH_PUMP_REASON_TANK_CEILING);
    /* collector protection before Tmax */
    FROM_OFF(1120, 720, 0, true, SH_PUMP_REASON_COL_PROTECT);
    /* Tmax before the difference and the minimum run time */
    FROM_ON(SH_PUMP_REASON_MIN_RUN, 900, 710, 0, false, SH_PUMP_REASON_TANK_MAX);
    /* invalid sensor before everything */
    FROM_ON(SH_PUMP_REASON_COL_PROTECT, INV, 950, 0, false, SH_PUMP_REASON_SENSOR_INVALID);
}

static void test_sensor_invalid(void)
{
    FROM_ON(SH_PUMP_REASON_DIFF_ON, INV, 400, 0, false, SH_PUMP_REASON_SENSOR_INVALID);
    FROM_ON(SH_PUMP_REASON_DIFF_ON, 600, INV, 0, false, SH_PUMP_REASON_SENSOR_INVALID);
    FROM_OFF(INV, INV, LONG_S, false, SH_PUMP_REASON_SENSOR_INVALID);
    FROM_OFF(1200, INV, LONG_S, false, SH_PUMP_REASON_SENSOR_INVALID);     /* no protection either */

    /* the latches are kept while a sensor is invalid */
    sh_pump_state_t st = off_state();
    STEP(st, &P, 800, 700, LONG_S, false, SH_PUMP_REASON_TANK_MAX);
    STEP(st, &P, 800, INV, LONG_S, false, SH_PUMP_REASON_SENSOR_INVALID);
    STEP(st, &P, 800, 690, LONG_S, false, SH_PUMP_REASON_TANK_MAX);
    /* valid again: the difference with the minimum times */
    st = off_state();
    STEP(st, &P, INV, 400, LONG_S, false, SH_PUMP_REASON_SENSOR_INVALID);
    STEP(st, &P, 600, 400, LONG_S, true,  SH_PUMP_REASON_DIFF_ON);
}

static void test_reason_str(void)
{
    for (int r = SH_PUMP_REASON_START; r <= SH_PUMP_REASON_MIN_OFF; r++) {
        s_checks++;
        if (strcmp(sh_pump_reason_str((sh_pump_reason_t)r), "?") == 0) {
            s_failed++;
            printf("FAIL: reason %d has no name\n", r);
        }
    }
}

int main(void)
{
    test_diff_on();
    test_diff_off();
    test_min_run();
    test_min_off();
    test_tank_max();
    test_col_min();
    test_tank_ceiling();
    test_col_protect();
    test_col_boiling();
    test_priorities();
    test_sensor_invalid();
    test_reason_str();
    printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
