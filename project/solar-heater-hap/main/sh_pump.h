/* Pump decision, normal mode (doc/operating-modes.md, 2.1) — plain C, no ESP-IDF / FreeRTOS,
 * so the same file is compiled into the host test (project/solar-heater-hap/test).
 *
 * Temperatures are tenths of °C (differences: tenths of K), times are seconds of a monotonic
 * clock. Order of the checks, first match wins:
 *   0. invalid temperature        -> off at once
 *   1. collector at boiling point -> the pump does not start for any reason
 *   2. tank emergency ceiling     -> off at once
 *   3. collector protection       -> on at once, also above Tmax
 *   4. tank Tmax                  -> normal charging off
 *   5. difference                 -> on / off, the only rule the minimum times apply to
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Firmware constants, tenths of °C */
#define SH_PUMP_HYST                20      /* return zone of every single-value limit, 2 K */
#define SH_PUMP_TANK_CEILING        950     /* off at >= 95 °C, released below 93 °C */
#define SH_PUMP_COL_PROTECT_CAP     1150    /* highest collector protection threshold in use */
#define SH_PUMP_COL_BOIL            1270    /* no start at >= 127 °C, released below 125 °C */

/* The rule that decided the pump output. */
typedef enum {
    SH_PUMP_REASON_NONE = 0,        /* no decision yet */
    SH_PUMP_REASON_SENSOR,          /* off: a temperature is invalid */
    SH_PUMP_REASON_COL_BOIL,        /* off: collector at the boiling point, start forbidden */
    SH_PUMP_REASON_TANK_CEILING,    /* off: tank at the emergency ceiling */
    SH_PUMP_REASON_COL_PROTECT,     /* on:  collector protection */
    SH_PUMP_REASON_TANK_MAX,        /* off: tank at Tmax */
    SH_PUMP_REASON_DIFF,            /* on / off: collector - tank difference */
    SH_PUMP_REASON_MIN_RUN,         /* on:  difference too low, minimum run time not over */
    SH_PUMP_REASON_MIN_OFF,         /* off: difference reached, minimum off time not over */
    SH_PUMP_REASON_COL_MIN,         /* off: difference reached, collector below its minimum */
} sh_pump_reason_t;

typedef struct {
    int16_t  dt_on;                 /* start at collector - tank >= this, 0.1 K */
    int16_t  dt_off;                /* stop at collector - tank <= this, 0.1 K */
    uint32_t min_run_s;             /* minimum run time, s */
    uint32_t min_off_s;             /* minimum off time, s (0 = none) */
    int16_t  tank_max;              /* tank Tmax, 0.1 °C */
    int16_t  col_min;               /* no start by the difference below this, 0.1 °C */
    int16_t  col_protect;           /* collector protection threshold, 0.1 °C */
} sh_pump_params_t;

/* Pump state carried between the calls; all zeros at start (pump off, nothing latched).
 * `on` and `reason` are the result of the last call, the rest is the memory of the limits. */
typedef struct {
    bool on;                        /* pump output */
    sh_pump_reason_t reason;        /* why */
    bool col_boil;                  /* collector reached the boiling point, not yet 2 K below it */
    bool tank_ceiling;              /* tank reached the ceiling, not yet 2 K below it */
    bool tank_max;                  /* tank reached Tmax, not yet 2 K below it */
    bool col_protect;               /* collector protection is running the pump */
} sh_pump_state_t;

/* One decision. `elapsed_s` is the time since `st->on` last changed (since the start, if it
 * never did); the caller restarts that time whenever the returned value differs from the
 * previous one. Updates `st` and returns the new `st->on`. */
bool sh_pump_decide(const sh_pump_params_t *p, sh_pump_state_t *st, int16_t t_col,
                    int16_t t_tank, uint32_t elapsed_s);

/* Short English name of the reason, for the log. */
const char *sh_pump_reason_name(sh_pump_reason_t reason);
