/* Pump decision, normal mode (doc/operating-modes.md 2.1) — plain C, no ESP-IDF / FreeRTOS, so the
 * same file is compiled into the host test (project/solar-heater-hap/test). The pump test, the
 * anti-seize run and the sensor-fault modes are in sh_pump_ctl, which calls this decision in the
 * normal mode.
 *
 * Temperatures are tenths of °C (differences: tenths of K), times are seconds of a monotonic clock.
 * Every single-value limit is active at >= value and released at < value - 2 K.
 * Order of the checks, first match wins:
 *   1. invalid temperature        -> off at once
 *   2. collector at boiling point -> off at once, also a running pump; no start for any reason
 *   3. tank emergency ceiling     -> off at once while the collector is at 95 °C too (until below
 *                                    93 °C); with a colder collector the pump can only take heat
 *                                    out of the tank, so the rules below decide as usual
 *   4. collector protection       -> on at once (no minimum off time), also above Tmax
 *   5. frost protection           -> on at once while the collector is below the frost threshold
 *                                    and the tank is at least 5 °C
 *   6. night cooling              -> while it is allowed (sh_cool: the night after a tank above
 *                                    Tmax or an E4, the nights of the vacation): on when the
 *                                    collector is colder than the tank by the switch-on
 *                                    difference, off below the switch-off difference — the same
 *                                    thresholds, the other direction, with the minimum times
 *   7. vacation                   -> normal charging off
 *   8. tank Tmax                  -> normal charging off at once
 *   9. heater leak (sh_heater)    -> during a leak check and while the leak is stopped: normal
 *                                    charging off at once (no minimum run time)
 *  10. difference                 -> on / off, with the minimum times; after a passed leak check
 *                                    the pump goes on at once as a running pump (on while the
 *                                    difference is above the switch-off one)
 * The boiling point, the ceiling of the collector protection and the frost threshold are computed
 * from the installer data (sh_installer) and come in with the parameters.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Firmware constants, tenths of °C */
#define SH_PUMP_RETURN_ZONE         20      /* return zone of every single-value limit, 2 K */
#define SH_PUMP_TANK_CEILING        950     /* off at >= 95 °C, released below 93 °C */
#define SH_PUMP_FROST_TANK_MIN       50      /* no frost protection with the tank below 5 °C */

/* The rule that decided the pump output. */
typedef enum {
    SH_PUMP_REASON_NONE = 0,        /* off: nothing decided yet */
    SH_PUMP_REASON_SENSOR,          /* off: a temperature is invalid */
    SH_PUMP_REASON_COL_BOILING,     /* off: collector at the boiling point */
    SH_PUMP_REASON_TANK_CEILING,    /* off: tank at the emergency ceiling */
    SH_PUMP_REASON_COL_PROTECT,     /* on:  collector protection */
    SH_PUMP_REASON_FROST,           /* on:  frost protection */
    SH_PUMP_REASON_TANK_MAX,        /* off: tank at Tmax */
    SH_PUMP_REASON_DIFF,            /* on / off: collector - tank difference */
    SH_PUMP_REASON_MIN_RUN,         /* on:  difference low, minimum run time not over */
    SH_PUMP_REASON_MIN_OFF,         /* off: difference enough, minimum off time not over */
    SH_PUMP_REASON_COL_MIN,         /* off: difference enough, collector below its minimum */
    /* sh_pump_ctl: pump test, anti-seize, sensor faults */
    SH_PUMP_REASON_TEST,            /* on:  pump test (buttons) */
    SH_PUMP_REASON_ANTI_SEIZE,      /* on:  anti-seize run after 24 h without running */
    SH_PUMP_REASON_BOTH_FAULT,      /* off: both sensors faulty */
    SH_PUMP_REASON_TF_RUN,          /* on:  tank sensor faulty, collector warm enough */
    SH_PUMP_REASON_TF_COLD,         /* off: tank sensor faulty, collector too cold */
    SH_PUMP_REASON_TF_HOT,          /* off: tank sensor faulty, collector at 95 °C, not yet below 93 °C */
    SH_PUMP_REASON_TF_PROBE,        /* on:  tank sensor faulty, probe at the collector emergency temperature */
    SH_PUMP_REASON_CF_PROBE,        /* on:  collector sensor faulty, probe (does the tank rise?) */
    SH_PUMP_REASON_CF_RUN,          /* on:  collector sensor faulty, the tank rises */
    SH_PUMP_REASON_CF_WAIT,         /* off: collector sensor faulty, waiting for the next probe */
    SH_PUMP_REASON_CF_HEATER,       /* off: collector sensor faulty, heater running, probe postponed */
    /* group C: night cooling, vacation, air venting */
    SH_PUMP_REASON_COOL,            /* on:  night cooling of the tank through the collector */
    SH_PUMP_REASON_VACATION,        /* off: vacation, no normal charging */
    SH_PUMP_REASON_AIR_RUN,         /* on:  air venting, run phase */
    SH_PUMP_REASON_AIR_PAUSE,       /* off: air venting, pause phase */
    /* group D: the heater */
    SH_PUMP_REASON_LEAK_CHECK,      /* off: leak check of the heater (5 min) */
    SH_PUMP_REASON_LEAK_STOP,       /* off: the leak of the heater stopped, no start by the difference */
    SH_PUMP_REASON_COUNT
} sh_pump_reason_t;

typedef struct {
    int16_t  diff_on;               /* start at collector - tank >= this, 0.1 K */
    int16_t  diff_off;              /* stop at collector - tank <= this (after min_run_s), 0.1 K */
    uint32_t min_run_s;             /* minimum run time, s */
    uint32_t min_off_s;             /* minimum off time, s (0 = none) */
    int16_t  tank_max;              /* tank Tmax, 0.1 °C */
    int16_t  col_min;               /* no start by the difference below this, 0.1 °C */
    int16_t  col_protect;           /* collector protection, 0.1 °C; capped at col_protect_cap */
    int16_t  col_protect_cap;       /* its ceiling, 0.1 °C (sh_limits_t.protect_cap) */
    int16_t  col_boiling;           /* boiling point, 0.1 °C: no start at >= this, released below it - 2 K */
    bool     frost_on;              /* frost protection enabled */
    int16_t  frost;                 /* frost threshold, 0.1 °C: on below it, off at >= it + 2 K */
    bool     cool;                  /* night cooling allowed now (sh_cool) */
    bool     charge_off;            /* vacation: no normal charging (col_protect is the vacation limit) */
    /* group D: the heater (sh_heater) */
    int16_t  heater_rise;           /* tank rise in 10 min with the pump off = heater, 0.1 K (at least 0.5 K) */
    bool     leak_check;            /* a leak check runs: normal charging off at once */
    bool     leak_stop;             /* the leak is stopped: no charging by the difference */
    bool     leak_resume;           /* the check passed: the pump goes on at once (this step) */
} sh_pump_params_t;

/* Pump state kept by the caller between the calls; all zeros at start (pump off, nothing latched).
 * `on` and `reason` are the result of the last call, the rest is the memory of the limits. */
typedef struct {
    bool on;                        /* pump output */
    sh_pump_reason_t reason;        /* why */
    bool col_boiling;               /* collector reached the boiling point, not yet below it - 2 K */
    bool tank_ceiling;              /* tank reached the ceiling, not yet below it - 2 K */
    bool tank_max;                  /* tank reached Tmax, not yet below it - 2 K */
    bool col_hot;                   /* collector reached the tank ceiling (95 °C), not yet below 93 °C */
    bool col_protect;               /* collector protection is running the pump */
    bool frost;                     /* frost protection is running the pump */
    bool cool;                      /* night cooling is running the pump */
} sh_pump_state_t;

/* A single-value limit with the 2 K return zone: true at t >= limit, false at t < limit - 2 K,
 * unchanged in between. */
bool sh_pump_latch(bool active, int16_t t, int16_t limit);

/* Updates the memory of the limits (boiling point, tank ceiling, Tmax) from every valid
 * temperature (SH_TEMP_INVALID leaves the limits of that sensor as they are). Called by
 * sh_pump_decide(); the caller may call it also when it does not decide (it is idempotent). */
void sh_pump_track_limits(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col, int16_t t_tank);

/* One decision. `elapsed_s` is the time since `st->on` last changed (since the start, if it never
 * did); the caller restarts that time whenever `st->on` changes. Updates `st`. */
void sh_pump_decide(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col, int16_t t_tank,
                    uint32_t elapsed_s);

/* The tank ceiling stops the pump: the tank at the ceiling and the collector hot too (with a colder
 * collector the pump takes heat out of the tank — allowed). */
bool sh_pump_ceiling_stop(const sh_pump_state_t *st);

/* Collector protection threshold in use: the parameter, capped. */
int16_t sh_pump_col_protect(const sh_pump_params_t *p);

/* Short English name of the reason, for the log. */
const char *sh_pump_reason_name(sh_pump_reason_t reason);
