/* History (doc/web-interface.md 4, 4.1; doc/tasks/step9-clock-history.md 3.2, 3.3) — plain C, no
 * ESP-IDF / FreeRTOS, compiled into the host test too. The flash partition and the lock are in
 * sh_history_store.
 *
 * Temperatures are tenths of °C, times are seconds of a monotonic clock; the local date and time
 * come in with every step (sh_clock_sync).
 *
 *   day chart   144 intervals of 10 min in RAM: the collector and the tank at the end of the
 *               interval and the seconds the pump ran, by kind (charging / protection / other).
 *               With the clock set the intervals end on the 10 min marks of the local time and the
 *               chart shows the ones since midnight; without the clock — the last 24 h.
 *   day         the extremes, the pump run time and the codes of the local day. At midnight, with
 *               the clock set, they make the daily summary; the collector peak of a whole day
 *               goes to the time zone check (sh_clock: sh_peak_day).
 *   summary     a record of 32 bytes with a fixed format (below), a version and a checksum
 *   log         the summaries in a flash partition used as a ring: the records follow each other,
 *               the next sector is erased when the last free slot is about to be written (the
 *               oldest 128 days go). The slot to write is always empty, so the place of the
 *               newest record is found again after any power loss.
 *   CSV         a line per summary: ';' between the fields, decimal point, English header
 *
 * Record, little-endian:
 *    0  u16  date, days since 2020-01-01 (local)
 *    2  i16  collector maximum, minimum; tank maximum, minimum (0.1 °C; SH_TEMP_INVALID: none)
 *   10  u16  pump minutes: charging, protection (collector, frost), other (test, anti-seize, probes)
 *   16  u16  night cooling minutes (group C; 0)
 *   18  u16  heater minutes (group D; 0)
 *   20  u16  codes of the day, SH_ALARM_BIT() of sh_alarm_t
 *   22  u16  bits 0-10 minute of the collector peak (0x7FF: none), bit 14 the day is not whole,
 *            bit 15 the clock is reliable
 *   24  u16  learned sunrise, sunset, minutes of the day (groups C / D; 0xFFFF: no data)
 *   28  u8   learned cooling target, °C (0xFF: no data)
 *   29  u8   version (1)
 *   30  u8   reserved (0)
 *   31  u8   CRC-8 (polynomial 0x07, start 0xFF) of bytes 0 - 30 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sh_pump_logic.h"
#include "sh_clock.h"

#define SH_HIST_SLOTS           144     /* intervals of the day chart */
#define SH_HIST_SLOT_S          600u    /* 10 min */
#define SH_HIST_DAY_S           86400u
#define SH_HIST_REC_SIZE        32
#define SH_HIST_VERSION         1
#define SH_HIST_SECTOR          4096u   /* flash erase unit */
#define SH_HIST_NONE16          0xFFFFu
#define SH_HIST_NONE8           0xFFu
#define SH_HIST_WEB_DAYS        7       /* the last days on the page */

/* What the pump ran for */
typedef enum {
    SH_RUN_CHARGE = 0,          /* difference, minimum run time, charging with a faulty sensor */
    SH_RUN_PROTECT,             /* collector protection (also the probe at a faulty tank sensor), frost */
    SH_RUN_OTHER,               /* pump test, anti-seize, probe at a faulty collector sensor */
    SH_RUN_KINDS,
    SH_RUN_OFF = SH_RUN_KINDS
} sh_run_t;

sh_run_t sh_run_kind(bool pump_on, sh_pump_reason_t reason);

/* ---- daily summary ---------------------------------------------------------------------- */

typedef struct {
    uint16_t day;               /* days since 2020-01-01 */
    int16_t col_max, col_min, tank_max, tank_min;   /* SH_TEMP_INVALID: no value that day */
    uint16_t run_min[SH_RUN_KINDS];
    uint16_t cool_min, heater_min;
    uint16_t alarms;
    uint16_t peak_min;          /* minute of the day; SH_PEAK_NONE */
    bool partial;               /* not observed from midnight */
    bool reliable;              /* the clock, after the check of this day */
    uint16_t sunrise, sunset;   /* SH_HIST_NONE16 */
    uint8_t cool_target;        /* SH_HIST_NONE8 */
} sh_hist_day_t;

void sh_hist_encode(const sh_hist_day_t *d, uint8_t rec[SH_HIST_REC_SIZE]);
/* false: wrong version or checksum (also an empty slot) */
bool sh_hist_decode(const uint8_t rec[SH_HIST_REC_SIZE], sh_hist_day_t *d);

/* CSV: the header and a line, each with its "\r\n"; the length, or -1 if `size` is too small. */
#define SH_HIST_CSV_LINE_MAX    160
const char *sh_hist_csv_header(void);
int sh_hist_csv_line(char *buf, size_t size, const sh_hist_day_t *d);

/* ---- log in flash ----------------------------------------------------------------------- */

/* The partition; offsets and lengths in bytes, erase by whole sectors. true: done. */
typedef struct {
    void *ctx;
    uint32_t size;              /* a multiple of SH_HIST_SECTOR, at least two sectors */
    bool (*read)(void *ctx, uint32_t off, uint8_t *buf, uint32_t len);
    bool (*write)(void *ctx, uint32_t off, const uint8_t *buf, uint32_t len);
    bool (*erase)(void *ctx, uint32_t off, uint32_t len);
} sh_hist_flash_t;

typedef struct {
    const sh_hist_flash_t *flash;
    uint16_t slots;             /* records the partition holds */
    uint16_t head;              /* the slot of the next record, always empty */
    uint16_t count;             /* slots in use (damaged ones too), the oldest right after the free ones */
} sh_hist_log_t;

/* Finds the place of the next record. Content that is not a log (no empty slot) is erased; a
 * sector whose erase was interrupted is erased again. false: the flash failed. */
bool sh_hist_log_open(sh_hist_log_t *log, const sh_hist_flash_t *flash);
bool sh_hist_log_append(sh_hist_log_t *log, const sh_hist_day_t *d);
/* Record `i` (0: the oldest ... count - 1: the newest); false: damaged. */
bool sh_hist_log_get(const sh_hist_log_t *log, uint16_t i, sh_hist_day_t *d);
/* The newest valid records, the newest first; how many. */
int sh_hist_log_last(const sh_hist_log_t *log, sh_hist_day_t *days, int max);
bool sh_hist_log_clear(sh_hist_log_t *log);

/* ---- day chart and day ------------------------------------------------------------------ */

typedef struct {
    uint32_t end_s;             /* monotonic */
    uint16_t min;               /* local minute of the day then; SH_HIST_NONE16: no clock */
    int16_t t_col, t_tank;      /* at the end; SH_TEMP_INVALID: faulty sensor, no value */
    uint16_t run_s[SH_RUN_KINDS];
} sh_hist_slot_t;

/* A point of the chart for the page */
typedef struct {
    int16_t x;                  /* end of the interval: minute of the day, or minutes ago (no clock) */
    int16_t t_col, t_tank;
    uint16_t run_s[SH_RUN_KINDS];
} sh_hist_point_t;

/* Inputs of one step (every second, and at every command) */
typedef struct {
    uint32_t now_s;
    int16_t t_col, t_tank;      /* last valid values; SH_TEMP_INVALID: none */
    bool col_fault, tank_fault;
    bool pump_on;
    sh_pump_reason_t reason;
    uint8_t alarms;             /* active codes */
    bool clock_set;             /* the next three are valid with it */
    uint16_t day;               /* local date */
    uint32_t sec;               /* local seconds since midnight */
    bool zone_confirmed;
} sh_hist_in_t;

/* Everything kept between the steps; all zeros at boot. */
typedef struct {
    bool started;
    uint32_t last_s;            /* previous step */
    sh_run_t last_kind;         /* the pump since then */

    sh_hist_slot_t slot[SH_HIST_SLOTS];     /* ring of the closed intervals */
    uint8_t head, n;            /* next slot, how many are used */
    uint32_t cur_start_s;       /* the open interval */
    uint16_t cur_run_s[SH_RUN_KINDS];

    bool clock_set;             /* the clock at the previous step ... */
    uint16_t day;               /* ... and the last local time taken from it, */
    uint32_t sec;
    uint32_t clock_s;           /* at this monotonic time */
    uint32_t midnight_s;        /* the local midnight of today, monotonic (0: before boot) */

    uint32_t day_start_s;       /* the day (without the clock: since boot) */
    bool partial;
    int16_t col_max, col_min, tank_max, tank_min;
    uint32_t peak_s;            /* when col_max was reached */
    uint32_t run_s[SH_RUN_KINDS];
    uint16_t alarms;

    sh_peak_t peak;             /* time zone check */
} sh_hist_t;

/* One step. true: a day ended at local midnight — `closed` is its summary (to be stored). */
bool sh_hist_step(sh_hist_t *h, const sh_hist_in_t *in, sh_hist_day_t *closed);

/* New zone, or the learned values are reset (sh_peak_reset). */
void sh_hist_peak_reset(sh_hist_t *h, bool zone_confirmed);

/* The clock is set and the time zone check does not doubt it. */
bool sh_hist_reliable(const sh_hist_t *h);

/* Seconds the pump ran today (without the clock: during the last 24 h), by kind. */
void sh_hist_run_today(const sh_hist_t *h, uint32_t run_s[SH_RUN_KINDS]);

/* The chart at the last step, the oldest point first; how many (up to SH_HIST_SLOTS).
 * `*day`: the points are of the local day (x: minute of the day), otherwise of the last 24 h
 * (x: minutes ago). */
int sh_hist_chart(const sh_hist_t *h, sh_hist_point_t *pts, bool *day);
