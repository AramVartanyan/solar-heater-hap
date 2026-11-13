/* The pulse mode: sensors on the pipes (doc/operating-modes.md 2.1, "Датчици на тръбите — импулсен
 * режим (т. 16)"; doc/tasks/step13-pulse-mode.md) — plain C, no ESP-IDF / FreeRTOS, compiled into
 * the host test too.
 *
 * On the pipes the sensors measure the liquid, and only while it flows: the values are reliable
 * once the pump has run for 30 s (a pulse, or any longer run). Everything that decides or shows a
 * temperature uses the last reliable values; the pipes with the pump stopped are used only inside
 * (frost gate, sunrise / sunset, heater sign, E8).
 *
 *   pulse     the pump runs for 30 s (the minimum run time of this mode); then the usual rules
 *             decide by the values as for a stopped pump (sh_pump_ctl): the pump goes on running
 *             or stops until the next pulse
 *   window    the pulses of the day: with a reliable clock 08:00 - 18:00 until a sunrise and a
 *             sunset are learned (the averages of sh_sun), then from the learned sunrise to the
 *             learned sunset; without it from the detected sunrise to the detected sunset
 *   day       in the window, by the collector of the last reliable values: below 50 °C every
 *             30 min, 50 - 80 °C every 10 min, above 80 °C every 3 min (at the threshold of the
 *             collector protection the pump stays on as the protection)
 *   overheat  the collector at the boiling point - 10 K or above: no pulses (between the pulses
 *             only the pipe is seen, and it shows much less than a stagnating collector) until
 *             the learned sunset (reliable clock); without a reliable clock for 5 h; with a
 *             reliable clock but nothing learned yet until 18:00 (the end of the fixed window
 *             stands for the sunset), but not earlier than 5 h after the last value that hot.
 *             The first pulse then comes at once.
 *   night     outside the window: a pulse every 3 h after the last reliable values
 *   cooling   while a night cooling session is active (sh_cool: the last reliable values show
 *             the trigger in its window; every night of the vacation): a pulse every 30 min.
 *             Without a trigger only the night pulse of 3 h remains — at night the tank only
 *             cools, so a value up to 3 h old rather overrates it.
 *   frost     the gate only starts the watch, outside the window: the collector pipe with the pump
 *             stopped falls below the frost threshold + 5 K -> a pulse at once; or a reliable
 *             value (the night pulse, the cooling) is below it. Then, whatever the gate, pulses by
 *             the reserve (the collector of the last pulse - the threshold): up to 5 K every
 *             30 min, up to 10 K every 60 min, above that every 120 min; an early pulse when the
 *             pipe with the pump stopped is 3 K or more below the collector of the last pulse.
 *             The watch ends when the window opens (the sunrise); the next night starts from the
 *             gate again. A pulse below the threshold starts the frost protection itself
 *             (sh_pump_logic).
 * The intervals count from the last reliable values (from any run of the pump); without any yet,
 * from the start of the mode (boot, or the installer switched it — then everything starts over).
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_PULSE_S              30u             /* a pulse: the values are reliable after it */

#define SH_PULSE_FROM_MIN       (8 * 60)        /* the window before anything is learned */
#define SH_PULSE_TO_MIN         (18 * 60)

#define SH_PULSE_WARM           500             /* collector of the last pulse: below -> 30 min */
#define SH_PULSE_HOT            800             /* up to and at it -> 10 min, above -> 3 min */
#define SH_PULSE_COLD_S         1800u
#define SH_PULSE_WARM_S         600u
#define SH_PULSE_HOT_S          180u
#define SH_PULSE_BOIL_MARGIN    100             /* no pulses at the boiling point - 10 K */
#define SH_PULSE_BLOCK_S        (5u * 3600u)    /* ... for 5 h without a learned sunset */

/* Until when the pulses are stopped after an overheat */
typedef enum {
    SH_PULSE_UNTIL_TIME = 0,    /* for 5 h (no reliable clock) */
    SH_PULSE_UNTIL_SUNSET,      /* until the learned sunset */
    SH_PULSE_UNTIL_FIXED,       /* reliable clock, nothing learned: until 18:00, at least 5 h */
} sh_pulse_until_t;

#define SH_PULSE_NIGHT_S        (3u * 3600u)    /* outside the window */
#define SH_PULSE_COOL_S         1800u           /* during a night cooling session */

#define SH_PULSE_FROST_GATE     50              /* frost pulses below the threshold + 5 K */
#define SH_PULSE_FROST_NEAR     50              /* reserve up to 5 K -> 30 min */
#define SH_PULSE_FROST_MID      100             /* up to 10 K -> 60 min, above -> 120 min */
#define SH_PULSE_FROST_NEAR_S   1800u
#define SH_PULSE_FROST_MID_S    3600u
#define SH_PULSE_FROST_FAR_S    7200u
#define SH_PULSE_FROST_EARLY    30              /* the pipe fell 3 K below the last pulse */

#define SH_PULSE_EVT_MODE       0x01    /* the mode was switched: everything starts over */
#define SH_PULSE_EVT_MEASURED   0x02    /* a run of the pump gave its first reliable values */
#define SH_PULSE_EVT_BLOCK      0x04    /* overheat: no pulses from now */
#define SH_PULSE_EVT_UNBLOCK    0x08    /* pulses again */

/* Inputs of one step; the pump as the last step left it. */
typedef struct {
    bool pipes;                 /* installer data: the sensors are on the pipes */
    int16_t t_col, t_tank;      /* measured now; SH_TEMP_INVALID when faulty or none */
    bool pump_on;
    uint32_t run_s;             /* how long the pump has been on (the relay), s */
    int16_t boiling;            /* the boiling point, 0.1 °C */
    /* sh_pulse_plan() only */
    bool reliable;              /* the clock is reliable ... */
    uint16_t min;               /* ... and its local minute */
    int rise, set;              /* learned averages (sh_sun_avg), local minutes, -1: none */
    bool sunrise, sunset;       /* sh_sun detected one in this step */
    bool cooling;               /* a night cooling session is active (sh_cool) */
    bool frost_on;              /* frost protection enabled ... */
    int16_t frost;              /* ... and its threshold, 0.1 °C */
} sh_pulse_in_t;

/* Kept between the steps; all zeros at boot. */
typedef struct {
    bool pipes;                 /* the mode of the last step */
    bool have;                  /* there are reliable values */
    int16_t col, tank;          /* the last reliable values (SH_TEMP_INVALID: none) */
    uint32_t meas_s;            /* when they were measured; without any: the start of the mode */
    bool fresh;                 /* this step: the pump has run for 30 s, the values are of now */
    bool was_fresh;
    bool block;                 /* overheat: no pulses ... */
    uint32_t block_s;           /* ... since the last reliable value that hot */
    sh_pulse_until_t block_until;   /* ... until when */
    bool unblocked;             /* the stop is over: the first pulse at once (the last values are the hot ones) */
    bool day;                   /* a sunrise was detected, no sunset since (window without a clock) */
    bool frost_watch;           /* the frost watch of this night runs (entered by the gate) */
    bool frost_now;             /* ... entered by the pipe: a pulse at once */
    bool due;                   /* a pulse is due now (sh_pulse_plan) */
    uint32_t next_s;            /* when the next one is expected, valid with next_known */
    bool next_known;
    uint8_t events;             /* SH_PULSE_EVT_* of the last step */
} sh_pulse_t;

/* The values are those of the liquid: the pump has been on for 30 s. */
bool sh_pulse_fresh(bool pump_on, uint32_t run_s);

/* The interval of the day by the collector of the last pulse (SH_TEMP_INVALID: none yet -> 30 min). */
uint32_t sh_pulse_day_interval(int16_t t_col);

/* The interval of the frost pulses by the reserve: the collector of the last pulse - the threshold. */
uint32_t sh_pulse_frost_interval(int32_t reserve);

/* The window of the pulses of the day. `day`: sh_pulse_t.day (used without a reliable clock). */
bool sh_pulse_window(bool reliable, uint16_t min, int rise, int set, bool day);

/* First half of a step, before anything uses the temperatures: takes the reliable values (`p->col`,
 * `p->tank`, `p->meas_s`) while the pump has run for 30 s and notes an overheat. A change of
 * `in->pipes` starts everything over. Without the pipes nothing else is done. */
void sh_pulse_measure(sh_pulse_t *p, const sh_pulse_in_t *in, uint32_t now_s);

/* Second half, after sh_sun and sh_cool: is a pulse due now? Also in `p->due`; `p->next_s` is when
 * the next pulse is expected (the early frost pulse and a window that opens by a detected sunrise
 * cannot be foreseen). */
bool sh_pulse_plan(sh_pulse_t *p, const sh_pulse_in_t *in, uint32_t now_s);
