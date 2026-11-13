/* Night cooling of the tank through the collector: when it is allowed, its target and the
 * self-learning (doc/operating-modes.md 4.1 "Нощно охлаждане на бойлера (т. 7)", 4.2 "Ваканция (т. 8)")
 * — plain C, no ESP-IDF / FreeRTOS, compiled
 * into the host test too. The pump itself is decided by sh_pump_logic (rule 6) while a session is
 * active: on when the collector is colder than the tank by the switch-on difference, off below
 * the switch-off difference.
 *
 *   triggers  a) the tank at or above Tmax (a seasonal setting: summer on, winter off):
 *                the window 04:00 - 07:00
 *             b) an E4 since the last night (always): the window 03:00 - 07:00
 *             c) the vacation: every night, from the sunset to the sunrise (sh_sun_night),
 *                no target — until the temperatures are equal
 *             Without a reliable clock the window of a) and b) is from 6 h to 10 h after the
 *             sunset detected in the same night (sh_sun: `dark`); without a detected sunset
 *             there is no cooling that night.
 *   session   a) / b) start in their window while the tank is above the target; one a night.
 *             They end when the tank reaches the target, when the window closes (07:00; without
 *             a clock at the sunrise, when the collector is above the tank again or 10 h after
 *             the sunset) or by the heater. During the session the sunrise is caught by the
 *             switch-off difference of the pump rule.
 *   heater    the tank rises >= 0.5 K above its lowest value while the pump runs by the cooling:
 *             the cooling stops until the next night; shown in the web interface
 *   target    a clear summer day gives 2.5 kWh/m² (flat) or 3.0 kWh/m² (tubes); the tank rises by
 *             dT = energy / (volume x 1.163 Wh/(l·K)); target = 95 °C - dT, held within
 *             Tmax - 20 K ... Tmax, then the learned correction, held within the same range
 *   learning  an E4 on the day after a night with an a) / b) session: correction -3 K; after 7
 *             mornings in a row without an E4: +1 K back towards the computed target (never above
 *             it). The correction is a learned value (NVS).
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_COOL_B_FROM_MIN      (3 * 60)        /* trigger b: from 03:00 */
#define SH_COOL_A_FROM_MIN      (4 * 60)        /* trigger a: from 04:00 */
#define SH_COOL_TO_MIN          (7 * 60)        /* both: until 07:00 */
#define SH_COOL_AFTER_SUNSET_S  (6u * 3600u)    /* without a reliable clock: from ... */
#define SH_COOL_NO_CLOCK_S      (4u * 3600u)    /* ... for this long (as 03:00 - 07:00) */
#define SH_COOL_HEATER_RISE     5               /* 0.5 K */

#define SH_COOL_FLAT_WH_M2      2500            /* a clear summer day */
#define SH_COOL_TUBES_WH_M2     3000
#define SH_COOL_TOP             950             /* the tank must stay below 95 °C */
#define SH_COOL_RANGE           200             /* target within Tmax - 20 K ... Tmax */
#define SH_COOL_E4_STEP         (-3)            /* K */
#define SH_COOL_QUIET_STEP      1               /* K */
#define SH_COOL_QUIET_DAYS      7
#define SH_COOL_OFFSET_MIN      (-20)           /* K: the width of the target range */

typedef enum {
    SH_COOL_NONE = 0,
    SH_COOL_TMAX,               /* a) tank above Tmax */
    SH_COOL_E4,                 /* b) E4 during the day */
    SH_COOL_VACATION,           /* c) vacation */
} sh_cool_trigger_t;

typedef enum {
    SH_COOL_END_NONE = 0,
    SH_COOL_END_TARGET,         /* the tank reached the target */
    SH_COOL_END_TIME,           /* the window closed (07:00, sunrise) / the night is over */
    SH_COOL_END_HEATER,         /* the heater works against the cooling */
    SH_COOL_END_SENSOR,         /* a temperature is not valid */
    SH_COOL_END_MODE,           /* the vacation was switched on or off */
} sh_cool_end_t;

#define SH_COOL_EVT_START       0x01
#define SH_COOL_EVT_END         0x02
#define SH_COOL_EVT_LEARNED     0x04    /* offset / quiet changed (to be stored) */

/* The target of triggers a) and b), tenths of °C. `area`: tenths of m², `offset`: the learned
 * correction, K. */
int16_t sh_cool_target(bool tubes, int16_t area, int16_t volume_l, int16_t tank_max, int8_t offset);

/* Inputs of one step. */
typedef struct {
    bool reliable;              /* the clock is reliable ... */
    uint16_t min;               /* ... and its local minute of the day */
    int16_t t_col, t_tank;      /* SH_TEMP_INVALID when faulty or none */
    int16_t tank_max;           /* Tmax, 0.1 °C */
    int16_t target;             /* sh_cool_target() with the learned correction */
    bool tmax_on;               /* trigger a) enabled in the current season */
    bool vacation;
    bool e4;                    /* E4 is active */
    bool night;                 /* sh_sun_night() */
    bool dark;                  /* sh_sun: sunset detected in this night ... */
    uint32_t sunset_s;          /* ... when */
    bool pump_cool;             /* the pump runs by the cooling */
} sh_cool_in_t;

/* Kept between the steps; all zeros at boot, then `offset` and `quiet` from NVS. */
typedef struct {
    sh_cool_trigger_t trigger;  /* the active session */
    int16_t target;             /* of the session (a, b), 0.1 °C */
    sh_cool_end_t end;          /* why the last session ended */
    bool heater;                /* heater seen: no cooling until the next night */
    bool e4_pending;            /* E4 since the last night */
    bool e4_seen;               /* E4 since the last morning (learning) */
    bool e4_was;
    bool cooled;                /* the last night had an a) / b) session (learning) */
    bool tonight;               /* this night has had its a) / b) session */
    bool was_window, was_night;
    bool run;                   /* heater detection: the pump runs by the cooling ... */
    int16_t run_min;            /* ... the lowest tank temperature since then */
    int8_t offset;              /* learned correction of the target, K (-20 ... 0) */
    uint8_t quiet;              /* mornings in a row without an E4 */
    uint8_t events;             /* SH_COOL_EVT_* of the last step */
    bool pulse;                 /* pulse mode: a session runs or may start by these values (its
                                 * window, the tank above the target / Tmax) — a pulse every 30 min */
} sh_cool_t;

/* One step at `now_s`. The pump may cool while `c->trigger != SH_COOL_NONE`. */
void sh_cool_step(sh_cool_t *c, const sh_cool_in_t *in, uint32_t now_s);
