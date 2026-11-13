/* Created by Aram Vartanyan (c) 2026 */

/* Pulse mode: the sensors on the pipes (doc/operating-modes.md 4.5, "Импулсен режим — датчици
 * на тръбите (т. 16)") — when the next pulse is due. Plain C, no
 * ESP-IDF / FreeRTOS, compiled into the host test too. The pulse itself, the reliable values and the
 * decisions by them are in sh_pump_ctl (sh_pump_params_t.pulse / pulse_due).
 *
 * With the pump stopped the pipe sensors do not see the collector and the tank; 30 s after the pump
 * started (a pulse or any other run) and as long as it runs they measure the liquid: these are the
 * reliable values (sh_pump_rel_t). A pulse is due (with the pump stopped) when, after the last
 * reliable value:
 *   window   with a reliable clock 08:00 - 18:00 until the sunrise and the sunset are learned, then
 *            from the learned sunrise to the learned sunset (the averages of sh_sun); without it
 *            from the sunrise to the sunset detected by sh_sun on the pipes
 *   day      in the window, by the reliable collector: below 50 °C 30 min, 50 - 80 °C 10 min,
 *            above 80 °C 3 min (at the collector protection the pump keeps running, sh_pump_logic)
 *   hot      a reliable collector at or above the boiling point - 10 K: no pulses at all (a warning
 *            in the web interface) — with a reliable clock until the learned sunset, without a
 *            learned one until 18:00 and not earlier than 5 h after; without a reliable clock not
 *            earlier than 5 h after (the last such value)
 *   night    outside the window: 3 h
 *   cooling  outside the window, while a night cooling session runs or may start by the last
 *            reliable values (sh_cool_t.pulse): 30 min
 *   frost    outside the window, with the frost protection on — the gate (threshold + 5 K) only
 *            starts the watch: the collector pipe with the pump stopped falls below it (a pulse at
 *            once), or a reliable value is below it (that pulse counts). During the watch, until
 *            the window opens, the pulses follow the margin of the reliable collector over the
 *            threshold: up to 5 K 30 min, up to 10 K 60 min, above 10 K 120 min (instead of the
 *            night pulse of 3 h), and one comes at once when the pipe falls >= 3 K below the
 *            reliable collector of the last pulse. The next night starts at the gate again.
 *   start    no reliable value yet (boot, the mounting changed): a pulse at once
 * The pulses of every kind renew the reliable values, so every interval counts from the last one.
 * Temperatures are tenths of °C, times seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_day.h"             /* sh_local_t */

#define SH_PULSE_RUN_S          30u             /* the pulse, also the minimum run time of the mode */

#define SH_PULSE_WIN_FROM_MIN   (8 * 60)        /* the window until the sun times are learned */
#define SH_PULSE_WIN_TO_MIN     (18 * 60)

#define SH_PULSE_WARM           500             /* the day interval by the collector: below 50 °C ... */
#define SH_PULSE_HOT            800             /* ... up to 80 °C, above */
#define SH_PULSE_COLD_S         (30u * 60u)
#define SH_PULSE_WARM_S         (10u * 60u)
#define SH_PULSE_HOT_S          (3u * 60u)
#define SH_PULSE_BOIL_MARGIN    100             /* no pulses at the boiling point - 10 K */
#define SH_PULSE_HOT_WAIT_S     (5u * 3600u)    /* without a reliable clock / learned sunset */

#define SH_PULSE_NIGHT_S        (3u * 3600u)
#define SH_PULSE_COOL_S         (30u * 60u)

#define SH_PULSE_GATE           50              /* frost: the gate, threshold + 5 K */
#define SH_PULSE_FROST_NEAR     50              /* margin up to 5 K: 30 min */
#define SH_PULSE_FROST_MID      100             /* up to 10 K: 60 min, above: 120 min */
#define SH_PULSE_FROST_NEAR_S   (30u * 60u)
#define SH_PULSE_FROST_MID_S    (60u * 60u)
#define SH_PULSE_FROST_FAR_S    (120u * 60u)
#define SH_PULSE_FROST_FALL     30              /* early pulse: the pipe fell 3 K */

#define SH_PULSE_NONE           0u              /* sh_pulse_t.next_s: no pulse planned */

#define SH_PULSE_EVT_HOT        0x01            /* no pulses: the collector was overheated */
#define SH_PULSE_EVT_HOT_END    0x02            /* pulses again */

/* The day interval by the reliable collector `col` (SH_TEMP_INVALID: 30 min). */
uint32_t sh_pulse_day_s(int16_t col);
/* The frost interval by the margin of the reliable collector over the threshold, 0.1 K. */
uint32_t sh_pulse_frost_s(int16_t margin);
/* The window of the pulses at the local minute `min` with a reliable clock; `rise` / `set`: the
 * learned averages (sh_sun_avg), -1: none (then 08:00 - 18:00). */
bool sh_pulse_window_at(int rise, int set, uint16_t min);

/* Inputs of one step; the pump and the reliable values as the last step left them. */
typedef struct {
    bool reliable;              /* the clock is reliable ... */
    sh_local_t lt;              /* ... the local time */
    int rise, set;              /* learned averages (sh_sun_avg), -1: none */
    bool risen;                 /* sh_sun: a sunrise detected, no sunset since (no clock) */
    bool pump_on;
    int16_t pipe_col;           /* the collector pipe now, SH_TEMP_INVALID: faulty / none */
    bool rel;                   /* there is a reliable value ... */
    int16_t rel_col;            /* ... its collector (SH_TEMP_INVALID: none / faulty) */
    uint32_t rel_s;             /* ... when */
    bool fresh;                 /* the reliable value is of the last step (the pump runs >= 30 s) */
    bool cool;                  /* a night cooling session runs or may start (sh_cool_t.pulse) */
    bool frost_on;              /* frost protection enabled ... */
    int16_t frost;              /* ... its threshold */
    int16_t boiling;            /* the boiling point */
} sh_pulse_in_t;

/* Kept between the steps; all zeros at boot and when the mounting changes. */
typedef struct {
    bool due;                   /* a pulse is due now (the pump stopped) */
    uint32_t next_s;            /* when the next one is due, SH_PULSE_NONE: none planned (hot) */
    bool window;                /* inside the window of the pulses */
    bool hot;                   /* no pulses after an overheated collector ... */
    uint32_t hot_s;             /* ... its last reliable value at the limit */
    uint16_t hot_day;           /* ... its local date (reliable clock) */
    bool watch;                 /* frost: the watch runs (the gate was passed this night) ... */
    uint32_t watch_s;           /* ... since; a pulse is due unless one came after it */
    uint8_t events;             /* SH_PULSE_EVT_* of the last step */
} sh_pulse_t;

void sh_pulse_step(sh_pulse_t *p, const sh_pulse_in_t *in, uint32_t now_s);
