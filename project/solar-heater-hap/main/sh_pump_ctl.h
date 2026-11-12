/* Pump control above the normal-mode decision (doc/operating-modes.md 2.1: pump test p. 11,
 * anti-seize p. 10, sensor fault p. 12; doc/tasks/step2-settings-menu-pump-sensors.md 3.3 - 3.5) —
 * plain C, no ESP-IDF / FreeRTOS, compiled into the host test as well.
 *
 * Temperatures are tenths of °C, times are seconds of a monotonic clock (0 = boot).
 *
 * Order, first match wins:
 *   1. pump test           -> on; only the boiling point and the tank ceiling stop it (and end it;
 *                             a test asked for while one of them is active ends at once)
 *   2. anti-seize run      -> on for 60 s; the same two limits stop and end it
 *   3. the mode chosen by the sensor faults:
 *        both faulty       -> off
 *        none faulty       -> sh_pump_decide() (normal mode, with the frost protection; it is
 *                             not checked while a sensor is faulty)
 *        tank faulty       -> by the collector: on at >= 60 °C, off below 58 °C, off at >= 95 °C
 *                             (until below 93 °C), probe at the collector emergency temperature
 *        collector faulty  -> probes of 10 min: does the tank rise >= 0.5 K?
 *   4. anti-seize start    -> after 24 h off, when the collector is at least as warm as the tank,
 *                             or after 12 h more whatever the temperatures
 * The end of the test and of the anti-seize run is handled like a stopped pump whose minimum off
 * time is over: the pump keeps running if the mode would start it (its minimum run time counts from
 * then), otherwise it stops and the minimum off time counts from then. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_pump_logic.h"

/* Pump test */
#define SH_PUMP_TEST_STEP_S         60          /* "+" / "-" during the test */
#define SH_PUMP_TEST_MAX_S          900         /* the remaining time at most */
#define SH_PUMP_TEST_MIN_S          60          /* the test never ends earlier after its start */

/* Anti-seize */
#define SH_SEIZE_IDLE_S             (24u * 3600u)   /* off this long -> run when collector >= tank */
#define SH_SEIZE_FORCE_S            (36u * 3600u)   /* off this long -> run whatever the temperatures */
#define SH_SEIZE_RUN_S              60u

/* Tank sensor faulty (the collector decides), tenths of °C */
#define SH_TF_START                 600         /* start at collector >= 60 °C */
#define SH_TF_STOP                  580         /* stop below 58 °C, after the minimum run time */
#define SH_TF_HOT                   950         /* stop at once at >= 95 °C, start again below 93 °C */
#define SH_TF_PROBE_RETRY_S         (5u * 3600u)    /* after a failed emergency probe */

/* Collector sensor faulty (the tank decides) */
#define SH_CF_WINDOW_S              600u        /* probe length and check period, 10 min */
#define SH_CF_RISE                  5           /* tank rise that counts, 0.5 K */
#define SH_CF_WAIT_S                1800u       /* between probes, doubled after failed ones ... */
#define SH_CF_WAIT_MAX_S            7200u       /* ... up to 2 h */
#define SH_CF_CEILING_WAIT_S        (5u * 3600u)    /* after the tank reached its ceiling */
#define SH_CF_HISTORY               11          /* tank samples, 1 min apart: the last 10 min */

/* Inputs of one step: the published measurement. */
typedef struct {
    int16_t t_col;              /* last valid value; SH_TEMP_INVALID if none since boot */
    int16_t t_tank;
    bool col_fault;             /* 5 failed measurements in a row */
    bool tank_fault;
} sh_pump_temps_t;

typedef enum {
    SH_PUMP_MODE_NORMAL = 0,
    SH_PUMP_MODE_TANK_FAULT,
    SH_PUMP_MODE_COL_FAULT,
    SH_PUMP_MODE_BOTH_FAULT,
} sh_pump_mode_t;

/* Events of the last step, for the log */
#define SH_PUMP_EVT_TEST_START      0x01
#define SH_PUMP_EVT_TEST_END        0x02
#define SH_PUMP_EVT_SEIZE_START     0x04
#define SH_PUMP_EVT_SEIZE_END       0x08

/* Everything the control keeps between the steps; all zeros at boot (pump off since 0 s). */
typedef struct {
    bool on;                    /* pump output */
    sh_pump_reason_t reason;    /* why */
    uint32_t switched_s;        /* when `on` last changed (0 = boot) */
    sh_pump_mode_t mode;
    sh_pump_state_t norm;       /* normal-mode decision and the memory of the limits */
    uint8_t events;             /* SH_PUMP_EVT_* of the last step or command */

    /* pump test */
    bool test;
    uint32_t test_start_s;
    uint32_t test_end_s;

    /* anti-seize */
    bool seize;
    uint32_t seize_start_s;

    /* tank sensor faulty */
    bool tf_hot;                /* collector reached 95 °C, not yet below 93 °C */
    bool tf_probe;              /* emergency probe running */
    uint32_t tf_probe_s;        /* its start */
    uint32_t tf_next_probe_s;   /* no emergency probe before */

    /* collector sensor faulty */
    bool cf_probe;              /* probe or run after a probe */
    bool cf_ceiling;            /* the 5 h wait of this tank-ceiling episode is set */
    bool cf_rise_seen;          /* the tank rose during this run */
    uint32_t cf_check_s;        /* start of the current 10 min window */
    int16_t cf_ref;             /* tank at that start */
    uint32_t cf_next_s;         /* no probe before */
    uint8_t cf_fails;           /* failed probes in a row */

    /* tank while the pump is off (heater detection), one sample a minute */
    int16_t hist[SH_CF_HISTORY];
    uint8_t hist_n;             /* samples since the pump stopped, up to SH_CF_HISTORY */
    uint8_t hist_head;          /* next slot */
    uint32_t hist_next_s;       /* next sample */
} sh_pump_ctl_t;

/* One step: decides `ctl->on` / `ctl->reason` at `now_s`. `p` are the current parameters. */
void sh_pump_ctl_step(sh_pump_ctl_t *ctl, const sh_pump_params_t *p, const sh_pump_temps_t *t,
                      uint32_t now_s);

/* Button commands, applied at `now_s`; the caller runs a step right after.
 * "+": starts the test for `test_s` seconds or, during the test, adds 60 s (at most 900 s left).
 * "-": during the test, takes 60 s away while more than 60 s are left.
 * stop: ends the test, but not earlier than 1 min after its start. */
void sh_pump_ctl_plus(sh_pump_ctl_t *ctl, uint32_t test_s, uint32_t now_s);
void sh_pump_ctl_minus(sh_pump_ctl_t *ctl, uint32_t now_s);
void sh_pump_ctl_stop(sh_pump_ctl_t *ctl, uint32_t now_s);

/* Seconds left of the test (0 without a test). */
uint32_t sh_pump_ctl_test_left(const sh_pump_ctl_t *ctl, uint32_t now_s);

/* The tank rose >= 0.5 K during the last 10 min with the pump off (heater running). */
bool sh_pump_ctl_heater(const sh_pump_ctl_t *ctl);
