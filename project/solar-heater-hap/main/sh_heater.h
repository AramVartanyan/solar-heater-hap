/* The electric heater of the tank: its sign and the heat leak through the pump
 * (doc/operating-modes.md 2.1, "Нагревател (т. 17)"; doc/tasks/step12-heater-leak-e8.md 3.2, 3.3) —
 * plain C, no ESP-IDF / FreeRTOS, compiled into the host test too.
 *
 *   sign      with the pump stopped the tank rises, over 10 min, by at least half of what the
 *             heater would give: power / (volume x 1.163 Wh/(l·K)) / 6 / 2, but not less than
 *             0.5 K (also with "0 kW"). The samples are those of sh_pump_ctl (the tank once a
 *             minute while the pump is off): sh_pump_ctl_heater() with this threshold.
 *   found     the sign with the setting "0 kW": the controller takes it that there is a heater
 *             (the leak logic applies) and shows it; the setting is not changed. Kept with the
 *             learned values.
 *   leak      only with a heater (power > 0 or found). The pump that charges by the difference
 *             may be carrying the heat of the heater to the collector:
 *     daylight  with a reliable clock: 09:00 - 16:00 local time, or — once a sunrise and a
 *               sunset are learned — from the sunrise + 1 h to the sunset - 1 h. In it the pump
 *               works as usual, no checks.
 *     schedule  out of it: a check after every 60 min of charging without a break — counted
 *               only out of the daylight: from the start of the pump, from the end of the
 *               daylight or from the last check — and after 30 min when that count starts in
 *               the last 2 h before the learned sunset (the daylight ends 1 h before it, so a
 *               pump that leaves the daylight is checked 30 and 60 min later).
 *               Without a reliable clock: after the collector has been falling for 30 min in
 *               a row (a sample a minute, none above the one before it) while the pump
 *               charges, and again after every 30 min of that.
 *     check     the pump is stopped for 5 min (the minimum times do not delay it, the
 *               protections act). The collector falls less than 2 K -> the pump goes on at once
 *               as a running pump (off only if the difference is at the switch-off value).
 *               It falls >= 2 K -> the heat came from the tank: no start by the difference
 *               until the collector warms up by itself (>= 2 K in 10 min with the pump stopped,
 *               sh_sun), until the next sunrise, or until the daylight begins.
 *             Only charging by the difference is checked — not the protections, the night
 *             cooling, the test, the air venting, the sensor-fault modes. A pump that starts
 *             for another reason during a check ends the check without a result.
 * Temperatures are tenths of °C, times seconds of a monotonic clock, minutes local. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_HEATER_RISE_MIN      5               /* 0.5 K: the smallest rise that counts */

#define SH_LEAK_FROM_MIN        (9 * 60)        /* the daylight before anything is learned */
#define SH_LEAK_TO_MIN          (16 * 60)
#define SH_LEAK_MARGIN_MIN      60              /* inwards from the learned sunrise / sunset */
#define SH_LEAK_PERIOD_S        3600u           /* charging without a break before a check */
#define SH_LEAK_EVENING_S       1800u           /* ... in the evening */
#define SH_LEAK_EVENING_MIN     120             /* the evening: this long before the sunset */
#define SH_LEAK_FALL_S          1800u           /* no clock: the collector falls this long */
#define SH_LEAK_CHECK_S         300u            /* the pump is stopped for the check */
#define SH_LEAK_DROP            20              /* 2 K: the collector fall that means a leak */

/* The tank rise in 10 min (0.1 K) that means "the heater works". `power`: tenths of kW (0: none). */
int16_t sh_heater_rise(int16_t power, int16_t volume_l);

/* There is a heater: by the setting, or found. */
bool sh_heater_present(int16_t power, bool found);

/* "Found" with the setting 0 kW: true when it has just been found (to be stored and logged). */
bool sh_heater_found(bool *found, int16_t power, bool sign);

/* The daylight in which the pump is not checked; `rise` / `set`: the learned averages, local
 * minutes, -1: none. */
bool sh_leak_daylight(uint16_t min, int rise, int set);

typedef enum {
    SH_LEAK_IDLE = 0,
    SH_LEAK_CHECK,              /* the pump is stopped for a check */
    SH_LEAK_STOPPED,            /* a leak was found: no start by the difference */
} sh_leak_state_t;

typedef enum {
    SH_LEAK_RES_NONE = 0,
    SH_LEAK_RES_SUN,            /* the collector held: sun */
    SH_LEAK_RES_LEAK,           /* the collector fell: the heat came from the tank */
} sh_leak_result_t;

#define SH_LEAK_EVT_CHECK       0x01    /* a check started */
#define SH_LEAK_EVT_SUN         0x02    /* its result: no leak, the pump goes on */
#define SH_LEAK_EVT_LEAK        0x04    /* its result: a leak, the pump stays off */
#define SH_LEAK_EVT_ABORT       0x08    /* ended without a result */
#define SH_LEAK_EVT_RELEASED    0x10    /* the pump may start by the difference again */

/* Inputs of one step: the pump as the last step left it. */
typedef struct {
    bool heater;                /* sh_heater_present() */
    bool reliable;              /* the clock is reliable ... */
    uint16_t min;               /* ... and its local minute of the day */
    int16_t rise, set;          /* learned sunrise / sunset, local minutes, -1: none */
    bool pump_on;
    bool charging;              /* the pump runs by the difference in the normal mode */
    int16_t t_col;              /* SH_TEMP_INVALID when faulty or none */
    bool warming;               /* the collector rose >= 2 K in 10 min with the pump stopped */
    bool sunrise;               /* a sunrise was detected in this step */
} sh_leak_in_t;

/* Kept between the steps; all zeros at boot. */
typedef struct {
    sh_leak_state_t state;
    bool counting;              /* charging out of the daylight ... */
    uint32_t since_s;           /* ... since then (or since the last check) */
    bool evening;               /* ... which was in the evening: the short period */
    bool falling;               /* no clock: the collector is being sampled ... */
    uint32_t fall_s;            /* ... it has not risen since then ... */
    int16_t fall_ref;           /* ... when it was this warm */
    int16_t fall_prev;          /* the last sample */
    uint32_t fall_next_s;       /* the next sample */
    uint32_t check_s;           /* start of the check */
    int16_t check_ref;          /* the collector at that start */
    int16_t drop;               /* the collector fall of the last finished check, 0.1 K */
    sh_leak_result_t last;      /* the result of the last finished check ... */
    uint32_t last_s;            /* ... and when */
    bool info;                  /* "the heater leak is stopped": until the pump starts again */
    /* the result of the step, for the pump */
    bool hold;                  /* keep the pump off (check) */
    bool block;                 /* no start by the difference (leak) */
    bool resume;                /* the check found no leak: decide as a running pump, at once */
    uint8_t events;             /* SH_LEAK_EVT_* of the last step */
} sh_leak_t;

/* One step at `now_s`, before the pump decision. */
void sh_leak_step(sh_leak_t *l, const sh_leak_in_t *in, uint32_t now_s);
