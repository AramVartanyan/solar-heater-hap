/* Seasons (doc/operating-modes.md 2.1.2, 2.2 — group C) — plain C, no ESP-IDF / FreeRTOS, compiled
 * into the host test too.
 *
 *   SSn   Sun / Cld: summer / winter by hand, never switched by the controller; Aut: recognised
 *   Aut   only from the current season to the other one (the two conditions do not fight):
 *           summer -> winter: the lowest collector temperature of the day (the night is in it)
 *                             below 5 °C, 3 days in a row
 *           winter -> summer: the collector above 60 °C while the pump has charged without a
 *                             break for over 1 h, 3 days in a row
 *         first: with a reliable clock in a time zone with daylight saving time, by it (summer
 *         time -> summer); the observations are for the zones without it and without a clock.
 *         A day is the local date with a set clock, otherwise 24 h; a day without a valid
 *         collector value (faulty sensor) does not count and does not break the series.
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_SEASON_COLD          50      /* collector minimum below 5 °C ... */
#define SH_SEASON_HOT           600     /* collector above 60 °C ... */
#define SH_SEASON_HOT_RUN_S     3600u   /* ... with the pump charging without a break for over 1 h */
#define SH_SEASON_DAYS          3       /* days in a row */

typedef struct {
    bool winter;                /* the recognised season (kept in NVS by the caller) */
    int16_t col_min;            /* lowest collector of the day, SH_TEMP_INVALID: none */
    bool hot;                   /* the hot-day condition was met today */
    bool charging;              /* the pump charges ... */
    uint32_t charge_s;          /* ... since */
    uint8_t cold_row, hot_row;
} sh_season_t;

/* `winter`: the stored season (summer at the first start). */
void sh_season_init(sh_season_t *s, bool winter);

/* Every step: the collector (SH_TEMP_INVALID: faulty / none) and whether the pump charges. */
void sh_season_observe(sh_season_t *s, int16_t t_col, bool charging, uint32_t now_s);

/* A day is over: the series; returns true when the season changed (to `s->winter`). */
bool sh_season_day(sh_season_t *s);

/* Reliable clock in a zone with daylight saving time: the season by it; returns true when it
 * changed. */
bool sh_season_by_dst(sh_season_t *s, bool dst);
