/* Sunrise and sunset from the collector (doc/operating-modes.md 2.1, p. 7 "Изгрев и залез" —
 * group C) — plain C, no ESP-IDF / FreeRTOS, compiled into the host test too.
 *
 *   sunrise  with the pump stopped the collector rises >= 2 K within 10 min (one sample a minute,
 *            the pump off for the whole 10 min), the first time after the local midnight (with a
 *            set clock) or after the sunset (without it); with a clock set during the day (not at
 *            a midnight) only before noon
 *   sunset   after the collector was above the tank, with the pump stopped the collector falls
 *            below the tank; a day without the collector above the tank has no sunset. A cloud
 *            can do the same during the day: the collector above the tank again (the pump
 *            stopped) cancels the night, and the next fall is the sunset — the last one of the
 *            day counts ("after the daily peak")
 *   night    from the sunset to the sunrise (or until the collector is above the tank again)
 * The local minutes of the day's sunrise and sunset go into the daily summary; with a set clock
 * the average of the last 7 days with a detected value is used (sh_sun_list_t), without it only
 * what was detected in the same night.
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_SUN_RISE         20      /* 0.1 K within the window */
#define SH_SUN_WINDOW_S     600u
#define SH_SUN_SAMPLES      11      /* one a minute: the last 10 min */
#define SH_SUN_NOON         720     /* local minute */
#define SH_SUN_NONE         (-1)
#define SH_SUN_DAYS         7       /* averaged */

typedef struct {
    int16_t t_col, t_tank;          /* SH_TEMP_INVALID: faulty / none */
    bool pump_on;
    bool clock;                     /* the local time is set */
    uint16_t day;                   /* local date (sh_day_number) */
    uint16_t min;                   /* local minute */
} sh_sun_in_t;

/* Events of a step */
#define SH_SUN_EVT_RISE     0x01
#define SH_SUN_EVT_SET      0x02
#define SH_SUN_EVT_DAY      0x04    /* the local date changed at a midnight (the day before is over) */

/* All zeros, then sh_sun_init(). */
typedef struct {
    int16_t col[SH_SUN_SAMPLES];    /* collector, the pump off */
    uint8_t n, head;
    uint32_t next_s;
    bool above;                     /* the collector was above the tank since the last sunset */
    bool night;
    bool armed;                     /* the sunrise is looked for */
    uint32_t sunset_s;              /* the last sunset (monotonic) */
    bool have_day;
    uint16_t day;                   /* local date of the values below */
    int16_t rise_min, set_min;      /* the day's sunrise and sunset (local), SH_SUN_NONE */
    int16_t done_rise, done_set;    /* those of the day before, after SH_SUN_EVT_DAY */
} sh_sun_t;

void sh_sun_init(sh_sun_t *s);

/* One step at `now_s`; returns SH_SUN_EVT_*. */
uint8_t sh_sun_step(sh_sun_t *s, const sh_sun_in_t *in, uint32_t now_s);

/* The detected values of the last days, newest first. */
typedef struct {
    int16_t v[SH_SUN_DAYS];
    uint8_t n;
} sh_sun_list_t;

void sh_sun_list_add(sh_sun_list_t *l, int16_t min);
int16_t sh_sun_list_avg(const sh_sun_list_t *l);   /* rounded; SH_SUN_NONE when empty */
