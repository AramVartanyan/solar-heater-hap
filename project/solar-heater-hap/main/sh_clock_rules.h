/* Clock rules (doc/operating-modes.md 7) — plain C, no ESP-IDF
 * / FreeRTOS, compiled into the host test too. sh_clock applies them.
 *
 *   browser time  the time sent by the page is taken only when the clock has not been set by SNTP
 *                 in the last 24 h (no internet, AP mode, after a power cut); a value outside
 *                 2020 ... 2099 is refused
 *   trust         not set (no SNTP, no browser) -> the logic uses the variants without a clock;
 *                 set, time zone not confirmed by the installer -> not reliable (the peak check
 *                 cannot make it reliable: UTC instead of the local time is only 2 - 3 h);
 *                 set, zone confirmed -> reliable at once, then by the peak check
 *   peak check    a full sunny day (collector >= 20 K above the minimum of the day) counts: its
 *                 peak (local minute of the highest collector temperature) in 10:00 - 17:00 is
 *                 "in"; 2 days in a row out -> not reliable, 2 in a row in -> reliable again;
 *                 other days (cloudy, partial, faulty collector sensor) are not counted and do
 *                 not break the series
 *   dates         day number = days since 2020-01-01 (the daily record, the CSV) */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_CLOCK_BROWSER_HOLD_S     (24u * 3600u)   /* SNTP keeps the browser out this long */
#define SH_CLOCK_EPOCH_MIN          1577836800LL    /* 2020-01-01 00:00 UTC */
#define SH_CLOCK_EPOCH_MAX          4102444800LL    /* 2100-01-01 00:00 UTC (excluded) */

#define SH_PEAK_SUNNY_RISE          200             /* 0.1 K above the minimum of the day */
#define SH_PEAK_FROM_MIN            (10 * 60)       /* the window of a normal peak, local minutes */
#define SH_PEAK_TO_MIN              (17 * 60)       /* (both ends included) */
#define SH_PEAK_DAYS                2               /* days in a row that change the trust */
#define SH_PEAK_NONE                (-1)

#define SH_DAY_NONE                 0xFFFFu         /* no day number */

/* The time the page sends: true if the clock takes it. `sntp_ever`: SNTP has set the clock since
 * boot, `sntp_s` when (monotonic s). */
bool sh_clock_browser_ok(int64_t epoch_s, bool sntp_ever, uint32_t sntp_s, uint32_t now_s);

typedef enum {
    SH_CLOCK_UNSET = 0,         /* never set since boot */
    SH_CLOCK_TZ_UNCONFIRMED,    /* set, the installer has not confirmed the time zone */
    SH_CLOCK_UNRELIABLE,        /* set, zone confirmed, the collector peaks do not fit it */
    SH_CLOCK_RELIABLE,
} sh_clock_trust_t;

sh_clock_trust_t sh_clock_trust(bool set, bool tz_confirmed, bool peak_reliable);

/* Kept in NVS (a learned value); sh_peak_reset() when the installer confirms a zone and by the
 * reset of the learned values. */
typedef struct {
    bool reliable;              /* by the peaks (with a confirmed zone) */
    uint8_t in_row;             /* counted days in a row with the peak in the window */
    uint8_t out_row;            /* ... out of it */
    int16_t peak;               /* local minute of the last counted day, SH_PEAK_NONE */
} sh_peak_t;

void sh_peak_reset(sh_peak_t *p, bool reliable);

typedef enum {
    SH_PEAK_NOT_COUNTED = 0,    /* not full, cloudy, or the collector sensor failed that day */
    SH_PEAK_IN,
    SH_PEAK_OUT,
} sh_peak_day_t;

/* A finished day: `full` collected with a set clock from midnight to midnight, `col_ok` the
 * collector sensor never failed, its minimum / maximum (0.1 °C) and the minute of the maximum. */
sh_peak_day_t sh_peak_day(sh_peak_t *p, bool full, bool col_ok, int16_t col_min, int16_t col_max,
                          int16_t peak_min);

/* Days since 2020-01-01 (proleptic Gregorian); SH_DAY_NONE out of 2020 ... 2099. */
uint16_t sh_day_number(int year, int month, int mday);
/* The date of a day number. */
void sh_day_date(uint16_t day, int *year, int *month, int *mday);
