/* Pump decision for the normal (heating) mode — pure C, no ESP-IDF / FreeRTOS, tested on the host
 * (project/solar-heater-hap/test). Rules: doc/operating-modes.md 2.1, doc/tasks/step1-control-loop.md.
 *
 * All temperatures in tenths of °C, all times in seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_PUMP_RETURN_ZONE         20      /* every single-value limit: active at >= value, released at < value - 2 K */
#define SH_PUMP_TANK_CEILING        950     /* emergency tank ceiling, fixed */
#define SH_PUMP_COL_PROTECT_CAP     1150    /* collector protection cap: min(120 - 5, 127 - 10) */
#define SH_PUMP_COL_BOILING         1270    /* no start at all (boiling point: water, 1.5 bar) */

typedef enum {
    SH_PUMP_REASON_START = 0,       /* off: initial state, nothing evaluated yet */
    SH_PUMP_REASON_SENSOR_INVALID,  /* off: a sensor has no valid reading */
    SH_PUMP_REASON_COL_BOILING,     /* off: collector at the boiling point (until < limit - 2 K) */
    SH_PUMP_REASON_TANK_CEILING,    /* off: tank at the emergency ceiling (until < 93 °C) */
    SH_PUMP_REASON_COL_PROTECT,     /* on:  collector protection */
    SH_PUMP_REASON_TANK_MAX,        /* off: tank at Tmax (until < Tmax - 2 K) */
    SH_PUMP_REASON_DIFF_ON,         /* on:  difference large enough */
    SH_PUMP_REASON_MIN_RUN,         /* on:  difference small, minimum run time not over */
    SH_PUMP_REASON_DIFF_LOW,        /* off: difference too small */
    SH_PUMP_REASON_COL_COLD,        /* off: difference enough, collector under its minimum */
    SH_PUMP_REASON_MIN_OFF,         /* off: difference enough, minimum off time not over */
} sh_pump_reason_t;

typedef struct {
    int16_t  diff_on;           /* start when T_col - T_tank >= this */
    int16_t  diff_off;          /* stop when T_col - T_tank <= this (after min_run_s) */
    uint32_t min_run_s;
    uint32_t min_off_s;
    int16_t  tank_max;          /* Tmax */
    int16_t  col_min;           /* no start by the difference below this collector temperature */
    int16_t  col_protect;       /* collector protection; capped at SH_PUMP_COL_PROTECT_CAP */
} sh_pump_params_t;

/* Pump state, kept by the caller between calls. The latches remember a reached limit until the
 * temperature leaves its return zone. Zero-initialised = pump off, nothing latched. */
typedef struct {
    bool             on;
    sh_pump_reason_t reason;
    bool             col_boiling;
    bool             tank_ceiling;
    bool             tank_max;
} sh_pump_state_t;

/* One decision step. t_col / t_tank: SH_TEMP_INVALID (-1000) when not valid.
 * elapsed_s: time since the pump last switched on or off (since boot before the first switch).
 * Updates *st (on, reason, latches). */
void sh_pump_decide(const sh_pump_params_t *p, int16_t t_col, int16_t t_tank, uint32_t elapsed_s,
                    sh_pump_state_t *st);

/* Collector protection threshold actually used (the parameter, capped). */
int16_t sh_pump_col_protect(const sh_pump_params_t *p);

const char *sh_pump_reason_str(sh_pump_reason_t reason);
