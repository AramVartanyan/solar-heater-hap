/* Created by Aram Vartanyan (c) 2026 */

/* The day in RAM (doc/web-interface.md 4, 4.1) — plain C,
 * no ESP-IDF / FreeRTOS, compiled into the host test too. Fed once a second by sh_history.
 *
 *   chart    144 intervals of 10 min: the collector and tank temperatures at the end of the
 *            interval and the pump run time by kind. With a set clock the intervals follow the
 *            local time (00:00 - 00:10, ...) and the chart is the day from midnight; without it
 *            they follow the time since boot and the chart is the last 24 h. Lost at restart.
 *   kinds    charging (difference, minimum run time, charging with a faulty sensor) / protection
 *            (collector protection, frost protection, the probe at the collector emergency
 *            temperature with a faulty tank sensor — Hot) / other (pump test, anti-seize run, the
 *            probe with a faulty collector sensor, air venting, the pulse of the pulse mode) /
 *            cooling (night cooling)
 *   summary  the day by the local time, only with a set clock: maximum / minimum of the collector
 *            and of the tank (faulty sensor: not taken), run time by kind, the alarm codes of the
 *            day, the minute of the collector maximum (the first one), the time with the heater
 *            sign active. At the first step of the next day (a normal midnight: the
 *            next date, before 01:00) the day is handed over to be written; it is full when it was
 *            collected with a set clock from midnight to midnight, otherwise partial. Any other
 *            change of the date (the clock was set to another day) drops the collected day; so
 *            does a jump of the local time of more than 1 h within the day (the clock set anew) —
 *            the day goes on as partial. Up to 1 h (daylight saving time) and small SNTP
 *            corrections the day goes on as it was.
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_pump_logic.h"

#define SH_CHART_SLOTS      144
#define SH_CHART_SLOT_S     600
#define SH_CHART_SLOT_MIN   10
#define SH_DAY_MIN          1440
#define SH_DAY_JUMP_MIN     61      /* a jump of the local time beyond this (min) = the clock set anew */

typedef enum {
    SH_RUN_CHARGE = 0,
    SH_RUN_PROTECT,
    SH_RUN_OTHER,
    SH_RUN_COOL,                /* night cooling (its own column in the daily record) */
    SH_RUN_KINDS
} sh_run_kind_t;

/* The kind of the running pump; -1 when it is off. */
int sh_run_kind(bool pump_on, sh_pump_reason_t reason);

/* The local time, from sh_clock. */
typedef struct {
    bool set;                   /* the clock is set (SNTP or browser) */
    uint16_t day;               /* local date, days since 2020-01-01 (sh_day_number) */
    uint16_t min;               /* local minute of the day, 0 ... 1439 */
} sh_local_t;

/* Inputs of one step (the published state). */
typedef struct {
    int16_t t_col, t_tank;      /* last valid values, SH_TEMP_INVALID: none */
    bool col_fault, tank_fault;
    bool pump_on;
    sh_pump_reason_t reason;
    uint16_t alarms;            /* SH_ALARM_BIT() of the active codes */
    uint16_t sunrises, sunsets; /* how many were detected since boot (sh_sun, counted by sh_control) */
    bool heater;                /* the heater sign is active */
} sh_day_in_t;

/* One finished interval. */
typedef struct {
    uint32_t end_s;             /* monotonic */
    uint16_t day;               /* local date of the interval, SH_DAY_NONE without a clock */
    uint8_t idx;                /* with a clock: the interval of the day, 0 ... 143 */
    int16_t t_col, t_tank;      /* at the end, SH_TEMP_INVALID: faulty / none */
    uint16_t run_s[SH_RUN_KINDS];
} sh_chart_slot_t;

/* The summary of a day. */
typedef struct {
    bool valid;                 /* something is collected */
    bool full;                  /* from midnight with a set clock, without a break */
    uint16_t day;
    int16_t col_max, col_min;   /* SH_TEMP_INVALID: no valid value that day */
    int16_t tank_max, tank_min;
    int16_t peak_min;           /* local minute of col_max, -1: none */
    bool col_fault;             /* the collector sensor was faulty at some time */
    uint32_t run_s[SH_RUN_KINDS];
    uint16_t alarms;
    int16_t sunrise, sunset;    /* local minutes of the detected ones (the last sunset), -1: none */
    uint32_t heater_s;          /* the heater sign was active, s */
} sh_day_sum_t;

typedef struct {
    sh_chart_slot_t slot[SH_CHART_SLOTS];   /* ring, oldest at `head` once full */
    uint16_t head, count;
    /* the running interval */
    bool started;
    uint32_t key;               /* which interval: by the local time or by the time since boot */
    uint16_t cur_day;
    uint8_t cur_idx;
    uint32_t cur_run_s[SH_RUN_KINDS];
    uint32_t last_s;            /* the previous step */
    bool last_set;              /* the clock was set at the previous step */
    int32_t last_local;         /* its local time, minutes since 2020-01-01 */
    int last_kind;              /* the pump kind since the previous step, -1 off */
    bool last_heater;           /* the heater sign since the previous step */
    uint16_t sunrises, sunsets; /* the counters at the previous step */
    sh_day_sum_t sum;           /* today */
} sh_day_t;

void sh_day_init(sh_day_t *d);

/* One step at `now_s`. Returns true when a day is over; it is copied to `done`. */
bool sh_day_step(sh_day_t *d, const sh_day_in_t *in, const sh_local_t *lt, uint32_t now_s,
                 sh_day_sum_t *done);

/* Run time "today" by kind, s: with a set clock since midnight (or since the clock was set), without
 * it the last 24 h; the running interval included. */
void sh_day_run_today(const sh_day_t *d, const sh_local_t *lt, uint32_t now_s,
                      uint32_t run_s[SH_RUN_KINDS]);

/* The intervals of the chart, oldest first: with a set clock those of today, without it those of
 * the last 24 h. `fn` gets each slot; returns how many. */
typedef void (*sh_chart_fn)(void *ctx, const sh_chart_slot_t *s);
unsigned sh_day_chart(const sh_day_t *d, const sh_local_t *lt, uint32_t now_s, sh_chart_fn fn, void *ctx);
