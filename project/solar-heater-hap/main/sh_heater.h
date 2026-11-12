/* The heater in the tank: its sign, the guaranteed daylight and the leak checks of the heater
 * (doc/operating-modes.md 2.1, "Нагревател (т. 17)"; doc/tasks/step12-heater-leak-e8.md 3.1 - 3.3) —
 * plain C, no ESP-IDF / FreeRTOS, compiled into the host test too.
 *
 *   sign       with the pump stopped the tank rose, over the last 10 min, by at least half of what
 *              the heater gives in 10 min: power / (volume x 1.163 Wh/(l·K)), but at least 0.5 K;
 *              with "0 kW" 0.5 K. The tank samples are those of sh_pump_ctl (sh_pump_ctl_heater()
 *              with the threshold of sh_heater_rise() in the pump parameters); the probes with a
 *              faulty collector sensor use the same sign.
 *   detected   "0 kW" and the sign was seen: the controller takes it that there is a heater (the
 *              leak logic applies) and shows it in the web interface; the setting is not changed.
 *              Kept in NVS with the learned values (sh_learn_store), cleared by their reset.
 *   daylight   the guaranteed daylight part of the day, only with a reliable clock: 09:00 - 16:00
 *              local time; once there are learned sunrise and sunset (the averages of sh_sun) from
 *              the sunrise + 1 h to the sunset - 1 h. Inside it the pump charges without checks.
 *   leak       only with a heater (power > 0 or detected). Outside the daylight, or without a
 *              reliable clock, a pump that charges (difference, minimum run time) is checked:
 *                with a reliable clock  after 60 min of charging outside the daylight; when the
 *                                       count starts in the evening (the last 2 h before the
 *                                       learned sunset) after 30 min; the count starts again
 *                                       after every check
 *                without it             after 30 min in a row in which the collector fell (one
 *                                       sample a minute; a rise starts the count again)
 *              The check: the pump stops for 5 min at once (the minimum times do not delay it, the
 *              protections act). The collector fell < 2 K -> the pump goes on at once (a running
 *              pump: on while the difference is above the switch-off one); >= 2 K -> the heat came
 *              from the tank: stop, and no start by the difference until the collector warms by
 *              itself (>= 2 K in 10 min with the pump stopped) or until the next sunrise. A check
 *              is given up when the pump runs during it (a protection, the test ...) or the
 *              collector value is lost.
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_HEATER_MIN_RISE      5               /* 0.5 K in 10 min */

#define SH_DAYLIGHT_FROM_MIN    (9 * 60)        /* without learned sunrise / sunset */
#define SH_DAYLIGHT_TO_MIN      (16 * 60)
#define SH_DAYLIGHT_MARGIN_MIN  60              /* inside the learned sunrise / sunset */
#define SH_LEAK_EVENING_MIN     120             /* the last 2 h before the learned sunset */

#define SH_LEAK_EVERY_S         3600u           /* charging outside the daylight before a check */
#define SH_LEAK_EVENING_S       1800u           /* ... in the evening */
#define SH_LEAK_FALL_MIN        30              /* without a clock: minutes of a falling collector */
#define SH_LEAK_CHECK_S         300u            /* the pump stops for this long */
#define SH_LEAK_DROP            20              /* the collector falls >= 2 K: leak */
#define SH_LEAK_WARM            20              /* the collector warms >= 2 K ... */
#define SH_LEAK_WARM_SAMPLES    11              /* ... in 10 min, a sample a minute */

/* The sign: the tank rise in 10 min that counts as a working heater, tenths of K. `power`: tenths
 * of kW (0: none), `volume_l`: the tank. */
int16_t sh_heater_rise(int16_t power, int16_t volume_l);

/* Detection with "0 kW"; all zeros at boot, then `detected` from NVS. */
typedef struct {
    bool detected;
} sh_heater_t;

/* One step with the sign of this step; returns true when the heater was detected just now (to be
 * stored and shown). */
bool sh_heater_step(sh_heater_t *h, int16_t power, bool sign);

/* The leak logic applies: a heater set (power > 0) or detected. */
bool sh_heater_present(const sh_heater_t *h, int16_t power);

/* The guaranteed daylight at the local minute `min` (a reliable clock); `rise` / `set`: the
 * learned averages, local minutes, -1: none (then 09:00 - 16:00). */
bool sh_daylight(int rise, int set, uint16_t min);

typedef enum {
    SH_LEAK_IDLE = 0,
    SH_LEAK_CHECK,              /* the pump is stopped for the check */
    SH_LEAK_STOP,               /* "the leak of the heater stopped": no start by the difference */
} sh_leak_phase_t;

#define SH_LEAK_EVT_CHECK       0x01    /* a check started */
#define SH_LEAK_EVT_PASS        0x02    /* the collector fell < 2 K: the pump goes on */
#define SH_LEAK_EVT_STOP        0x04    /* >= 2 K: stopped */
#define SH_LEAK_EVT_ABORT       0x08    /* the check was given up */
#define SH_LEAK_EVT_END         0x10    /* the stop ended (warmed by itself, sunrise, no heater) */

/* Inputs of one step; the pump as the last step left it. */
typedef struct {
    bool heater;                /* sh_heater_present() */
    bool reliable;              /* the clock is reliable ... */
    uint16_t min;               /* ... and its local minute */
    int rise, set;              /* learned averages (sh_sun_avg), -1: none */
    int16_t t_col;              /* SH_TEMP_INVALID when faulty or none */
    bool pump_on;
    bool charging;              /* the pump runs by the difference / minimum run time (normal mode) */
    bool sunrise;               /* sh_sun detected the sunrise in this step */
} sh_leak_in_t;

/* Kept between the steps; all zeros at boot. */
typedef struct {
    sh_leak_phase_t phase;
    bool resume;                /* this step: the check passed, the pump goes on at once */
    /* with a reliable clock: charging outside the daylight since */
    bool counting;
    uint32_t run_s;
    uint32_t every_s;           /* 60 or 30 min (evening), set when the count starts */
    /* without it: the collector falling, a sample a minute */
    uint8_t fall_n;             /* minutes in a row without a rise */
    int16_t fall_last, fall_first;
    uint32_t fall_next_s;
    /* the check */
    uint32_t check_s;
    int16_t check_ref;          /* the collector at its start */
    /* the stop: the collector with the pump stopped, a sample a minute */
    int16_t warm[SH_LEAK_WARM_SAMPLES];
    uint8_t warm_n, warm_head;
    uint32_t warm_next_s;
    /* the last check (web interface) */
    bool last;                  /* there was one */
    bool last_stop;             /* its result */
    int16_t last_drop;          /* the collector fell by, 0.1 K */
    uint8_t events;             /* SH_LEAK_EVT_* of the last step */
} sh_leak_t;

void sh_leak_step(sh_leak_t *l, const sh_leak_in_t *in, uint32_t now_s);

/* Seconds left of a running check (0 without one). */
uint32_t sh_leak_left(const sh_leak_t *l, uint32_t now_s);
