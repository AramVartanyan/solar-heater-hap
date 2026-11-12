/* Sunrise and sunset seen by the collector (doc/operating-modes.md 2.1, "Нощно охлаждане (т. 7)" —
 * изгрев и залез; doc/tasks/step10-cooling-vacation-seasons.md 3.2) — plain C, no ESP-IDF /
 * FreeRTOS, compiled into the host test too.
 *
 *   sunrise  with the pump stopped the collector rises >= 2 K in 10 min (one sample a minute, all
 *            with the pump stopped); the first time after midnight and before noon (a rise after
 *            a restart in the afternoon is not a sunrise) — without a set clock the first time
 *            after the sunset
 *   sunset   after the collector has been above the tank, with the pump stopped, it falls below
 *            the tank. Later in the day it may rise above the tank and fall again: the last fall
 *            of the day is the one after the daily peak. A day in which the collector was never
 *            above the tank has no sunset.
 *   dark     from the sunset until the sunrise (or until the collector is above the tank again)
 *   learned  with a reliable clock the local minutes of the last 7 detected sunrises and of the
 *            last 7 detected sunsets are kept; their averages are what the logic uses. Without a
 *            reliable clock only what was detected in the same night counts (`dark`).
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_day.h"             /* sh_local_t */

#define SH_SUN_RISE_K           20      /* 2 K ... */
#define SH_SUN_RISE_SAMPLES     11      /* ... over 10 min, a sample a minute */
#define SH_SUN_RISE_BEFORE_MIN  (12 * 60)   /* with a set clock: a sunrise is before noon */
#define SH_SUN_DAYS             7
#define SH_SUN_NONE             0xFFFFu /* no time */

#define SH_SUN_EVT_RISE         0x01    /* sunrise detected in this step */
#define SH_SUN_EVT_SET          0x02    /* sunset detected in this step */
#define SH_SUN_EVT_LEARNED      0x04    /* the learned times changed (to be stored) */

/* Learned (kept in NVS): local minutes, newest last, SH_SUN_NONE: empty. */
typedef struct {
    uint16_t rise[SH_SUN_DAYS];
    uint16_t set[SH_SUN_DAYS];
} sh_sun_days_t;

void sh_sun_days_reset(sh_sun_days_t *d);
/* The average of the stored times, -1 if none. */
int sh_sun_avg(const uint16_t v[SH_SUN_DAYS]);

/* All zeros at boot, then sh_sun_days_reset() or the stored days in `days`. */
typedef struct {
    int16_t hist[SH_SUN_RISE_SAMPLES];  /* collector while the pump is off, one sample a minute */
    uint8_t n, head;
    uint32_t next_s;
    bool above;                 /* the collector has been above the tank since the last sunset */
    bool dark;                  /* sunset detected, no sunrise since */
    bool warming;               /* in the last step: the collector rose >= 2 K in 10 min, pump stopped */
    uint32_t sunset_s;          /* when (valid while dark) */
    bool rise_done;             /* the sunrise of this day / of this night is taken */
    uint16_t rise_day;          /* its local date (with a set clock) */
    uint16_t set_day, set_min;  /* the last sunset of a day with a reliable clock, not stored yet */
    bool set_pending;
    sh_sun_days_t days;
} sh_sun_t;

/* One step. `t_col` / `t_tank`: SH_TEMP_INVALID when faulty or none; `lt`: the local time
 * (lt->set false: no clock); `reliable`: the clock is reliable (the learned times are taken only
 * then). Returns SH_SUN_EVT_*. */
uint8_t sh_sun_step(sh_sun_t *s, int16_t t_col, int16_t t_tank, bool pump_on, const sh_local_t *lt,
                    bool reliable, uint32_t now_s);

/* Night (from the sunset to the sunrise): with a reliable clock and both learned averages by the
 * local minute, otherwise what was detected in this night. */
bool sh_sun_night(const sh_sun_t *s, const sh_local_t *lt, bool reliable);
