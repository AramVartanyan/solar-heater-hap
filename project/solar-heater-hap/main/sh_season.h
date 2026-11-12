/* Seasons: the automatic choice between summer and winter (doc/operating-modes.md 2.1.2;
 * doc/tasks/step10-cooling-vacation-seasons.md 3.4) — plain C, no ESP-IDF / FreeRTOS, compiled into
 * the host test too. The seasonal values themselves are in sh_settings.
 *
 *   Sun / Cld   the season set by hand, never changed
 *   Aut         with a reliable clock in a time zone that has daylight saving time: by it
 *               (summer time -> summer). Otherwise by the collector, only from the current season
 *               to the other one, over periods of 24 h (each holds one night and one day):
 *                 summer -> winter  the lowest collector temperature of the period below 5 °C,
 *                                   3 periods in a row
 *                 winter -> summer  the collector above 60 °C while the pump runs and has run
 *                                   without a break for more than 1 h, 3 periods in a row
 *               The season kept from before (NVS; summer at first) stays until then.
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_settings.h"        /* sh_season_mode_t */

#define SH_SEASON_PERIOD_S      (24u * 3600u)
#define SH_SEASON_COLD          50          /* below 5 °C at night */
#define SH_SEASON_HOT           600         /* above 60 °C ... */
#define SH_SEASON_HOT_RUN_S     3600u       /* ... with the pump running longer than this */
#define SH_SEASON_ROW           3           /* periods in a row */

/* All zeros at boot. */
typedef struct {
    bool started;
    uint32_t period_s;          /* start of the running period */
    bool have_min;
    int16_t col_min;            /* lowest collector temperature of the period */
    bool hot;                   /* the summer sign was seen in the period */
    bool run;                   /* the pump runs ... */
    uint32_t run_s;             /* ... since */
    uint8_t cold_row, hot_row;
} sh_season_t;

/* One step; returns the season to use (true: winter). `winter`: the current one; `t_col`:
 * SH_TEMP_INVALID when faulty or none; `dst_known`: the clock is reliable and its zone has
 * daylight saving time, `dst`: it is in force now. */
bool sh_season_step(sh_season_t *s, sh_season_mode_t mode, bool winter, int16_t t_col, bool pump_on,
                    bool dst_known, bool dst, uint32_t now_s);
