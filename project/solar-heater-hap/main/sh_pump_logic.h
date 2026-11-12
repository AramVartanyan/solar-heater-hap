/* Pump decision (doc/operating-modes.md 2.1) — plain C, no ESP-IDF / FreeRTOS, so the same file is
 * compiled into the host test (project/solar-heater-hap/test).
 *
 * Temperatures are tenths of °C (differences: tenths of K), times are seconds of a monotonic clock.
 * Every single-value limit is active at >= value and released at < value - 2 K.
 *
 * Forced runs come first; only the collector at the boiling point and the tank emergency ceiling
 * stop or refuse them (a limit whose sensor is faulty or has no value yet cannot be checked):
 *   - pump test (item 11): started by the "+" button, runs for the set time, at least 1 min
 *   - anti-seize (item 10): 60 s after 24 h without a run, when the collector is at least as warm
 *     as the tank; after 12 h more whatever the temperatures
 * When a forced run ends the control goes on as from a stopped pump whose minimum off time is over.
 *
 * Then, by the sensors:
 *   both valid         -> normal mode, first match wins:
 *                           1. collector at boiling point -> off at once, also a running pump
 *                           2. tank emergency ceiling     -> off at once
 *                           3. collector protection       -> on at once (no minimum off time)
 *                           4. tank Tmax                  -> normal charging off at once
 *                           5. difference                 -> the only rule with the minimum times
 *   tank faulty        -> charging by the collector alone: on at >= 60 °C, off below 58 °C (minimum
 *                         times apply); off at once at >= 95 °C, on again below 93 °C; at the
 *                         collector protection threshold a probe run of the minimum run time
 *   collector faulty   -> probe runs of 10 min that go on while the tank rises >= 0.5 K per 10 min
 *   both faulty        -> off
 *   no value yet       -> off (a sensor that never answered, until it is declared faulty)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Firmware constants, tenths of °C */
#define SH_PUMP_RETURN_ZONE         20      /* return zone of every single-value limit, 2 K */
#define SH_PUMP_TANK_CEILING        950     /* off at >= 95 °C, released below 93 °C */
#define SH_PUMP_COL_PROTECT_CAP     1150    /* min(collector max 120 - 5, boiling point 127 - 10) */
#define SH_PUMP_COL_BOILING         1270    /* boiling point, defaults: water, 1.5 bar; released below 125 °C */

/* Pump test, s */
#define SH_PUMP_TEST_MIN_S          60      /* a started test runs at least this long */
#define SH_PUMP_TEST_STEP_S         60      /* "+" / "-" during the test */
#define SH_PUMP_TEST_MAX_S          900     /* the most "+" can leave */

/* Anti-seize, s */
#define SH_PUMP_SEIZE_IDLE_S        (24 * 3600) /* without a run */
#define SH_PUMP_SEIZE_WAIT_S        (12 * 3600) /* more, waiting for collector >= tank */
#define SH_PUMP_SEIZE_RUN_S         60

/* Tank sensor faulty */
#define SH_PUMP_TF_START            600     /* charging on at collector >= 60 °C, off below 58 °C */
#define SH_PUMP_TF_HOT              950     /* off at collector >= 95 °C, on again below 93 °C */

/* Collector sensor faulty */
#define SH_PUMP_CF_WINDOW_S         600     /* probe run, and every check after it */
#define SH_PUMP_CF_RISE             5       /* tank rise per window that shows sun (or a heater), 0.5 K */
#define SH_PUMP_CF_WAIT_S           1800    /* between probes; doubles after failed probes... */
#define SH_PUMP_CF_WAIT_MAX_S       7200    /* ...up to this */

/* After a stop with the tank (or, tank faulty, the collector) at 95 °C: no probe for this long */
#define SH_PUMP_PROBE_LOCK_S        (5 * 3600)

/* Tank temperatures remembered while the pump is off (heater detection) */
#define SH_PUMP_HIST_PERIOD_S       60
#define SH_PUMP_HIST_COUNT          (SH_PUMP_CF_WINDOW_S / SH_PUMP_HIST_PERIOD_S + 1)

/* The rule that decided the pump output. */
typedef enum {
    SH_PUMP_REASON_NONE = 0,        /* off: nothing decided yet */
    SH_PUMP_REASON_SENSOR,          /* off: both sensors faulty, or a sensor without a value yet */
    SH_PUMP_REASON_COL_BOILING,     /* off: collector at the boiling point */
    SH_PUMP_REASON_TANK_CEILING,    /* off: tank at the emergency ceiling */
    SH_PUMP_REASON_COL_PROTECT,     /* on:  collector protection */
    SH_PUMP_REASON_TANK_MAX,        /* off: tank at Tmax */
    SH_PUMP_REASON_DIFF,            /* on / off: collector - tank difference */
    SH_PUMP_REASON_MIN_RUN,         /* on:  would stop, minimum run time not over */
    SH_PUMP_REASON_MIN_OFF,         /* off: would start, minimum off time not over */
    SH_PUMP_REASON_COL_MIN,         /* off: difference enough, collector below its minimum */
    SH_PUMP_REASON_TEST,            /* on:  pump test */
    SH_PUMP_REASON_ANTI_SEIZE,      /* on:  anti-seize run */
    SH_PUMP_REASON_TANK_FAULT,      /* on / off: tank sensor faulty, charging by the collector alone */
    SH_PUMP_REASON_TANK_FAULT_HOT,  /* off: tank sensor faulty, collector reached 95 °C */
    SH_PUMP_REASON_TANK_FAULT_PROBE,/* on:  tank sensor faulty, probe at the collector protection threshold */
    SH_PUMP_REASON_COL_FAULT_PROBE, /* on:  collector sensor faulty, probe run */
    SH_PUMP_REASON_COL_FAULT_CHARGE,/* on:  collector sensor faulty, the tank keeps rising */
    SH_PUMP_REASON_COL_FAULT_WAIT,  /* off: collector sensor faulty, waiting for the next probe */
    SH_PUMP_REASON_COL_FAULT_LOCK,  /* off: collector sensor faulty, 5 h after the tank ceiling */
    SH_PUMP_REASON_HEATER,          /* off: collector sensor faulty, the tank rises by itself (heater) */
    SH_PUMP_REASON_COUNT
} sh_pump_reason_t;

typedef struct {
    int16_t  diff_on;               /* start at collector - tank >= this, 0.1 K */
    int16_t  diff_off;              /* stop at collector - tank <= this (after min_run_s), 0.1 K */
    uint32_t min_run_s;             /* minimum run time, s */
    uint32_t min_off_s;             /* minimum off time, s (0 = none) */
    int16_t  tank_max;              /* tank Tmax, 0.1 °C */
    int16_t  col_min;               /* no start by the difference below this, 0.1 °C */
    int16_t  col_protect;           /* collector protection, 0.1 °C; capped at SH_PUMP_COL_PROTECT_CAP */
    uint16_t test_s;                /* pump test time, s */
} sh_pump_params_t;

/* What the measurement publishes: the last valid value of each sensor (SH_TEMP_INVALID before the
 * first one) and whether the sensor is declared faulty (its value is then old). */
typedef struct {
    int16_t t_col;
    int16_t t_tank;
    bool    col_fault;
    bool    tank_fault;
} sh_pump_input_t;

typedef enum {
    SH_PUMP_FORCED_NONE = 0,
    SH_PUMP_FORCED_TEST,
    SH_PUMP_FORCED_ANTI_SEIZE,
} sh_pump_forced_t;

/* Pump state kept by the caller between the calls; all zeros at start (pump off since second 0,
 * nothing latched). `on` and `reason` are the result of the last call, the rest is memory. */
typedef struct {
    bool on;                        /* pump output */
    sh_pump_reason_t reason;        /* why */
    uint32_t changed_s;             /* when `on` last changed (0: the start) */

    bool col_boiling;               /* collector reached the boiling point, not yet below it - 2 K */
    bool tank_ceiling;              /* tank reached the ceiling, not yet below it - 2 K */
    bool tank_max;                  /* tank reached Tmax, not yet below it - 2 K */
    bool col_protect;               /* collector protection is running the pump */

    sh_pump_forced_t forced;        /* pump test / anti-seize run in progress */
    uint32_t forced_start_s;
    uint32_t test_end_s;            /* set end of the test ("+" / "-" / "menu" move it) */

    bool tf_hot;                    /* tank faulty: collector reached 95 °C, not yet below 93 °C */
    bool tf_probe;                  /* tank faulty: probe run in progress */
    uint32_t tf_probe_s;            /* its start */
    uint32_t tf_lock_s;             /* no new probe before this time */

    bool cf_active;                 /* collector faulty: the mode was active in the last call */
    bool cf_proved;                 /* the current run has passed its probe window */
    bool cf_ceiling;                /* the 5 h lock of this tank ceiling episode is set */
    uint8_t cf_fails;               /* consecutive probes without a rise */
    uint32_t cf_window_s;           /* start of the current 10 min window */
    int16_t cf_ref;                 /* tank temperature at that moment */
    uint32_t cf_next_s;             /* no new probe before this time (wait between probes) */
    uint32_t cf_lock_s;             /* no new probe before this time (5 h after the ceiling) */

    uint8_t hist_n;                 /* tank temperature every minute since the pump stopped, oldest first */
    struct {
        uint32_t s;
        int16_t t;
    } hist[SH_PUMP_HIST_COUNT];
} sh_pump_state_t;

/* One decision at the time `now_s`. Updates `st`. */
void sh_pump_decide(const sh_pump_params_t *p, sh_pump_state_t *st, const sh_pump_input_t *in,
                    uint32_t now_s);

/* Buttons on the normal screen; call sh_pump_decide() right after, so the pump follows at once.
 *   "+":    starts the test (refused while a limit that stops it is active); during the test
 *           adds 60 s, the time left is at most 900 s
 *   "-":    during the test takes 60 s off, only while more than 60 s are left
 *   "menu": during the test ends it (the pump still runs until 1 min after the start)
 * "-" and "menu" return false when no test is running: the key is not for the pump. */
void sh_pump_test_plus(const sh_pump_params_t *p, sh_pump_state_t *st, const sh_pump_input_t *in,
                       uint32_t now_s);
bool sh_pump_test_minus(sh_pump_state_t *st, uint32_t now_s);
bool sh_pump_test_stop(sh_pump_state_t *st, uint32_t now_s);

/* Seconds until the running test stops the pump (what the display counts down); 0 without a test. */
uint32_t sh_pump_test_left(const sh_pump_state_t *st, uint32_t now_s);

/* Collector protection threshold in use: the parameter, capped. */
int16_t sh_pump_col_protect(const sh_pump_params_t *p);

/* Short English name of the reason, for the log. */
const char *sh_pump_reason_name(sh_pump_reason_t reason);
