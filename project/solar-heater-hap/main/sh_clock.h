/* Clock rules (doc/operating-modes.md 3.1; doc/tasks/step9-clock-history.md 3.1) — plain C, no
 * ESP-IDF / FreeRTOS, compiled into the host test too. The system clock, SNTP and the stored zone
 * are in sh_clock_sync.
 *
 *   browser time   taken only if the clock was not set by SNTP during the last 24 h
 *   time zones     the supported IANA names with their POSIX TZ rules (daylight saving time)
 *   dates          local date <-> day number (days since 2020-01-01), as the history keeps it
 *   peak check     the time zone is not taken on trust: on a sunny day (collector >= 20 K above
 *                  its minimum of the day) the collector peak must be between 10:00 and 17:00
 *                  local time. 2 sunny days in a row outside -> the clock is unreliable; 2 in a
 *                  row inside -> reliable again; cloudy days neither count nor break the row. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SH_CLOCK_SNTP_FRESH_S   (24u * 3600u)   /* the browser does not touch an SNTP time this fresh */
#define SH_CLOCK_SNTP_PERIOD_S  (12u * 3600u)   /* SNTP again this often */

/* The time of the browser may set the clock. `sntp` — the clock was set by SNTP since boot,
 * `since_sntp_s` — how long ago. */
bool sh_clock_browser_ok(bool sntp, uint32_t since_sntp_s);

typedef struct {
    const char *name;           /* IANA name, as the browser gives it */
    const char *tz;             /* POSIX TZ rule */
} sh_zone_t;

extern const sh_zone_t sh_zones[];
extern const int sh_zone_count;
#define SH_ZONE_NONE            (-1)    /* no zone confirmed: UTC */

int sh_zone_find(const char *name);     /* index, or SH_ZONE_NONE */

/* Day number of a date (days since 2020-01-01; earlier dates give 0) and back. */
uint16_t sh_clock_day(int year, int month, int day);
void sh_clock_date(uint16_t day_no, int *year, int *month, int *day);

#define SH_PEAK_SUNNY           200     /* collector rise of a sunny day, 0.1 K */
#define SH_PEAK_FROM_MIN        (10 * 60)   /* the peak is in order from 10:00 ... */
#define SH_PEAK_TO_MIN          (17 * 60)   /* ... to 17:00, both included */
#define SH_PEAK_DAYS            2       /* sunny days in a row that change the verdict */
#define SH_PEAK_NONE            0xFFFFu /* no peak (minute of the day) */

typedef struct {
    bool reliable;
    uint8_t in_row, out_row;    /* sunny days in a row with the peak inside / outside */
    uint16_t last_min;          /* peak of the last sunny day, minute of the day; SH_PEAK_NONE */
    bool last_in;               /* it was inside */
} sh_peak_t;

/* New clock, new zone, or the learned values are reset: reliable at once with a confirmed zone;
 * the check goes on. */
void sh_peak_reset(sh_peak_t *p, bool zone_confirmed);

/* A whole day is over: the collector extremes (0.1 °C) and the minute of its maximum. */
void sh_peak_day(sh_peak_t *p, int16_t col_max, int16_t col_min, uint16_t peak_min);
