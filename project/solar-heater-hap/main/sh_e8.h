/* E8: reverse thermosiphon through a broken gravity check valve (doc/operating-modes.md 2.1,
 * "Аварийни съобщения (т. 13)" — A8, "Признакът на A8 / `E8` — числата")
 * — plain C, no ESP-IDF / FreeRTOS, compiled into the host
 * test too. E8 is only shown; the pump logic does not change.
 *
 *   night        sh_sun_night() (the learned sunrise / sunset with a reliable clock, otherwise the
 *                sunset detected in this night)
 *   sign         at night, with the pump stopped for at least 1 h (one sample a minute, all at night
 *                with the pump stopped and both sensors valid), over that hour:
 *                  the collector rose >= 2 K,
 *                  the tank fell by at least half of the thermosiphon rate: the loss of the
 *                  collector (flat 4, evacuated tubes 1.5 W/m²K) x the area x 40 K /
 *                  (volume x 1.163 Wh/(l·K)), K/h,
 *                  the tank fell faster than the learned normal cooling (while there is one),
 *                  the difference collector - tank shrank (its absolute value is smaller)
 *   nights       a night with the sign: the sign was met at least once; a normal night: at least
 *                one such hour without the sign; a night with neither (the pump ran, a faulty
 *                sensor) does not count. The night is judged when it ends.
 *   learned      the normal cooling: the average tank fall (K/h) of the last 7 normal nights, from
 *                the minutes with the pump stopped, valid sensors and no heater sign; kept in NVS
 *                with the learned values (sh_learn_store)
 *   E8           declared after 3 nights in a row with the sign, cleared after 3 normal nights in
 *                a row; the counters and E8 are kept in NVS.
 * Temperatures are tenths of °C, rates tenths of K/h, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_E8_SAMPLES           61      /* 1 h, a sample a minute */
#define SH_E8_COL_RISE          20      /* 2 K in the hour */
#define SH_E8_NIGHTS            3       /* in a row: declared / cleared */
#define SH_E8_DAYS              7       /* normal nights learned */
#define SH_E8_NO_RATE           INT16_MIN
#define SH_E8_DT_K              40      /* night difference collector - outside, K */
#define SH_E8_FLAT_LOSS         40      /* W/m²K, tenths */
#define SH_E8_TUBES_LOSS        15

#define SH_E8_EVT_SIGN          0x01    /* the first sign of this night */
#define SH_E8_EVT_NIGHT         0x02    /* a night was judged (with the sign / normal) */
#define SH_E8_EVT_CHANGED       0x04    /* the learned values / counters / E8 changed (to be stored) */

/* Half of the thermosiphon rate: the least tank fall of the sign, tenths of K/h, rounded up.
 * `area`: tenths of m². */
int16_t sh_e8_threshold(bool tubes, int16_t area, int16_t volume_l);

/* Kept in NVS. */
typedef struct {
    int16_t rate[SH_E8_DAYS];   /* normal cooling of the tank, newest last, SH_E8_NO_RATE: empty */
    uint8_t sign_nights;        /* nights with the sign in a row */
    uint8_t normal_nights;      /* normal nights in a row */
    bool active;                /* E8 */
} sh_e8_learned_t;

void sh_e8_learned_reset(sh_e8_learned_t *l);       /* nothing learned, no E8 */
/* The learned normal cooling: the average of the stored rates, SH_E8_NO_RATE if none. */
int16_t sh_e8_normal(const sh_e8_learned_t *l);

typedef struct {
    bool night;                 /* sh_sun_night() */
    int16_t t_col, t_tank;      /* SH_TEMP_INVALID when faulty or none */
    bool pump_on;
    bool heater;                /* the heater sign (sh_pump_ctl_heater) */
    int16_t threshold;          /* sh_e8_threshold() */
} sh_e8_in_t;

/* All zeros at boot, then `l` from NVS (or sh_e8_learned_reset()). */
typedef struct {
    sh_e8_learned_t l;
    int16_t col[SH_E8_SAMPLES], tank[SH_E8_SAMPLES];
    uint8_t n, head;
    uint32_t next_s;
    bool was_night;
    bool sign;                  /* this night had the sign */
    bool hour;                  /* this night had an hour without it */
    int32_t fall_sum;           /* the tank fall of the minutes counted for the normal cooling, 0.1 K */
    uint16_t fall_min;
    bool fall_last_ok;
    int16_t fall_last;
    int16_t last_rate;          /* the tank fall of the last hour judged, 0.1 K/h (web, log) */
    uint8_t events;
} sh_e8_t;

void sh_e8_step(sh_e8_t *e, const sh_e8_in_t *in, uint32_t now_s);
