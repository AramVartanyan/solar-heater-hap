/* Night cooling and the vacation (doc/operating-modes.md 2.1, p. 7 and p. 8, 2.2 — group C;
 * doc/tasks/step10-cooling-vacation-seasons.md 3.2, 3.3) — plain C, no ESP-IDF / FreeRTOS, compiled
 * into the host test too. It decides when a cooling session runs; inside a session the pump is
 * switched by sh_pump_decide() (the reverse difference, the minimum times).
 *
 * Triggers:
 *   a) Tmax      the tank above Tmax (the Tmax limit active) and the seasonal switch on: from 04:00
 *   b) E4        E4 was active since the last night: from 03:00, in both seasons
 *   c) vacation  the whole night, from the sunset to the sunrise; the target is the equalisation
 * a) and b) with a reliable clock by the local time, until 07:00 at the latest; without it 6 h
 * after the detected sunset (no sunset — no cooling that night). Their target: sh_cool_target()
 * (the installer data and the learned correction); a tank already at the target does not start
 * a session. A session ends at the target (a, b: done for the night), at the sunrise, at 07:00
 * (a, b), at the end of the vacation night or of the vacation, when a sensor fails — and when the
 * tank rises >= 0.5 K within 10 min in the session (a heater: no more cooling until the next
 * night, shown in the web interface). The vacation night with a reliable clock: from the average
 * sunset to the average sunrise of the last 7 days; without the averages or the clock: the night
 * of sh_sun. A new night (the sunset, or 12:00 with a reliable clock) clears "done" and the heater
 * stop; the end of the a/b window (07:00, the sunrise) clears the E4 of the day before (unless it
 * is still active).
 *
 * Learning (kept in NVS by the caller): E4 on the day after a night with a) / b) (before the next
 * sunset) -> the target -3 K; 7 days in a row without E4 -> +1 K back towards the computed one
 * (never above it); at most -20 K.
 * Temperatures are tenths of °C, times seconds of a monotonic clock, local minutes of the day. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_COOL_E4_FROM         (3 * 60)    /* local minutes */
#define SH_COOL_TMAX_FROM       (4 * 60)
#define SH_COOL_UNTIL           (7 * 60)
#define SH_COOL_NEW_NIGHT       (12 * 60)
#define SH_COOL_AFTER_SUNSET_S  (6u * 3600u)
#define SH_COOL_HEATER_RISE     5           /* 0.1 K within 10 min */
#define SH_COOL_SAMPLES         11          /* one a minute: the last 10 min */
#define SH_COOL_LEARN_DOWN      30          /* 0.1 K */
#define SH_COOL_LEARN_UP        10
#define SH_COOL_LEARN_MIN       (-200)
#define SH_COOL_LEARN_DAYS      7

typedef enum {
    SH_COOL_NONE = 0,
    SH_COOL_TMAX,               /* a */
    SH_COOL_E4,                 /* b */
    SH_COOL_VACATION,           /* c */
} sh_cool_why_t;

typedef enum {
    SH_COOL_END_NONE = 0,
    SH_COOL_END_TARGET,         /* the tank reached the target */
    SH_COOL_END_SUNRISE,        /* the sunrise, or the end of the vacation night */
    SH_COOL_END_MORNING,        /* 07:00 */
    SH_COOL_END_HEATER,         /* the tank rises: a heater */
    SH_COOL_END_MODE,           /* the vacation switched on or off */
    SH_COOL_END_SENSOR,         /* no valid temperature */
} sh_cool_end_t;

typedef struct {
    bool clock;                 /* reliable clock: `min` is the local time */
    uint16_t min;
    bool night;                 /* sh_sun: after the sunset, before the sunrise */
    uint32_t sunset_s;          /* its time (monotonic) */
    bool sunrise, sunset;       /* events of this step (sh_sun) */
    int16_t avg_rise, avg_set;  /* averages of the last 7 days, SH_SUN_NONE */
    int16_t t_col, t_tank;      /* SH_TEMP_INVALID: faulty / none */
    bool tank_max;              /* the Tmax limit is active */
    bool night_cool;            /* trigger a) on (the switch of the season) */
    bool e4;                    /* E4 active */
    bool vacation;
    int16_t target;             /* of a) / b) (sh_cool_target) */
} sh_cool_in_t;

/* The learned correction (kept in NVS). */
typedef struct {
    int16_t offset;             /* 0.1 K, SH_COOL_LEARN_MIN ... 0 */
    uint8_t ok_days;            /* days in a row without E4 */
} sh_cool_learn_t;

#define SH_COOL_EVT_START       0x01
#define SH_COOL_EVT_END         0x02
#define SH_COOL_EVT_LEARNED     0x04    /* `learn` changed */

/* All zeros at boot. */
typedef struct {
    sh_cool_why_t why;          /* the session running, SH_COOL_NONE */
    int16_t target;             /* its target, SH_TEMP_INVALID: equalisation (vacation) */
    int16_t until;              /* its end by the clock (local minute), SH_SUN_NONE: the sunrise */
    bool done;                  /* a) / b) over for this night */
    bool heater;                /* stopped by a heater until the next night */
    bool e4_seen;               /* E4 since the last night */
    bool watch;                 /* a) / b) ran: an E4 before the next sunset lowers the target */
    bool e4_prev, e4_day;       /* E4 edge; E4 during the day (learning) */
    int16_t last_min;           /* the local minute of the previous step, -1: no clock */
    int16_t tank[SH_COOL_SAMPLES];
    uint8_t n, head;
    uint32_t next_s;
    /* the last step */
    uint8_t events;
    sh_cool_why_t ended;        /* the session that ended */
    sh_cool_end_t end;
} sh_cool_t;

/* One step at `now_s`; may change `learn` (SH_COOL_EVT_LEARNED). */
void sh_cool_step(sh_cool_t *c, sh_cool_learn_t *learn, const sh_cool_in_t *in, uint32_t now_s);

/* A day is over (the local date, or 24 h without a clock): the 7 days without E4. */
void sh_cool_day(sh_cool_t *c, sh_cool_learn_t *learn);

/* The vacation night (`in` as for the step). */
bool sh_cool_vacation_night(const sh_cool_in_t *in);

const char *sh_cool_why_name(sh_cool_why_t why);
const char *sh_cool_end_name(sh_cool_end_t end);
