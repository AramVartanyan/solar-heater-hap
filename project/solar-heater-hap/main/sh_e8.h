/* E8 — reverse thermosiphon through a failed gravity brake (doc/operating-modes.md 2.1, "Аварийни
 * съобщения (т. 13)", A8 and its numbers; doc/tasks/step12-heater-leak-e8.md 3.4) — plain C, no
 * ESP-IDF / FreeRTOS, compiled into the host test too.
 *
 *   sign      at night (sh_sun_night), with the pump stopped and both sensors valid for the last
 *             hour (a sample a minute), over that hour:
 *               the collector rose >= 2 K (at night it normally only cools), and
 *               the tank fell by at least half of the computed thermosiphon rate, and
 *               the tank fell faster than its learned normal cooling (if any is learned), and
 *               the collector and the tank came closer to each other.
 *   rate      the loss of the collector, flat 4 / evacuated tubes 1.5 W/m²K x area x 40 K,
 *             over volume x 1.163 Wh/(l·K); the threshold is half of it, at least 0.1 K/h
 *             (1.8 m² flat, 120 l: ~2 K/h -> 1.0 K/h).
 *   nights    a night with the sign: the sign was there at least once (counted at once);
 *             a normal night: no sign and at least one full hour that could be judged
 *             (counted when the night ends); any other night is not counted and does not break
 *             the series.
 *   E8        declared with the 3rd night in a row with the sign, cleared after 3 normal nights
 *             in a row.
 *   learned   the normal cooling of the tank, 0.1 K/h: of every normal night the average fall
 *             over its minutes with the pump stopped, valid sensors and no heater sign; the
 *             average of the last 7 such nights is used.
 * The counters, E8 itself and the learned nights are kept with the learned values (NVS).
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_E8_WINDOW_MIN        60              /* the hour that is judged */
#define SH_E8_COL_RISE          20              /* 2 K: the collector over that hour */
#define SH_E8_FLAT_W_M2K        40              /* loss of the collector, 0.1 W/m²K: flat 4 ... */
#define SH_E8_TUBES_W_M2K       15              /* ... evacuated tubes 1.5 */
#define SH_E8_NIGHT_DIFF_K      40              /* the night difference the loss is taken at */
#define SH_E8_FALL_MIN          1               /* 0.1 K/h: the smallest threshold of the tank fall */
#define SH_E8_NIGHTS            3               /* in a row: to declare and to clear */
#define SH_E8_LEARN_NIGHTS      7
#define SH_E8_NO_RATE           0xFFu           /* an empty learned night */
#define SH_E8_RATE_MAX          250             /* 25 K/h: the highest stored rate */

#define SH_E8_EVT_SIGN          0x01    /* the first sign of this night */
#define SH_E8_EVT_NORMAL        0x02    /* a normal night ended */
#define SH_E8_EVT_SKIPPED       0x04    /* a night that is not counted ended */
#define SH_E8_EVT_ON            0x08    /* E8 declared */
#define SH_E8_EVT_OFF           0x10    /* E8 cleared */
#define SH_E8_EVT_STORE         0x20    /* the kept values changed (to be stored) */

/* The tank fall in an hour (0.1 K) that fits a thermosiphon: half of its computed rate. `area`:
 * tenths of m². */
int16_t sh_e8_fall(bool tubes, int16_t area, int16_t volume_l);

/* Kept in NVS. */
typedef struct {
    bool active;                        /* E8 */
    uint8_t sign_nights;                /* nights in a row with the sign, up to SH_E8_NIGHTS */
    uint8_t normal_nights;              /* normal nights in a row, likewise */
    uint8_t rate[SH_E8_LEARN_NIGHTS];   /* normal cooling of the last normal nights, 0.1 K/h, newest
                                           last; SH_E8_NO_RATE: empty */
} sh_e8_keep_t;

void sh_e8_keep_reset(sh_e8_keep_t *k);
/* The learned normal cooling, 0.1 K/h; -1 if nothing is learned. */
int sh_e8_normal(const sh_e8_keep_t *k);

/* Inputs of one step: the pump as this step left it. */
typedef struct {
    bool night;                 /* sh_sun_night() */
    bool pump_on;
    int16_t t_col, t_tank;      /* SH_TEMP_INVALID when faulty or none */
    bool heater;                /* the heater sign is active */
    int16_t fall;               /* sh_e8_fall() */
} sh_e8_in_t;

/* All zeros at boot, then sh_e8_keep_reset() or the stored values in `keep`. */
typedef struct {
    sh_e8_keep_t keep;
    bool night;                 /* the night goes on */
    bool sign;                  /* this night has had the sign */
    bool judged;                /* this night has had a full hour that could be judged */
    int16_t col[SH_E8_WINDOW_MIN + 1];  /* a sample a minute while the pump is off */
    int16_t tank[SH_E8_WINDOW_MIN + 1];
    uint8_t n, head;
    uint32_t next_s;            /* the next sample */
    bool have_prev;             /* the minute before could be measured too ... */
    int16_t prev_tank;          /* ... the tank then */
    int32_t fall_sum;           /* this night: the tank fall over the measured minutes, 0.1 K ... */
    uint16_t fall_min;          /* ... and how many they are */
    uint8_t events;             /* SH_E8_EVT_* of the last step */
} sh_e8_t;

/* One step at `now_s`, after the pump decision. Returns E8 (also in `e->keep.active`). */
bool sh_e8_step(sh_e8_t *e, const sh_e8_in_t *in, uint32_t now_s);
