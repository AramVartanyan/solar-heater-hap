/* Alarms and information (doc/operating-modes.md 2.1, p. 13; doc/led-indication.md p. 6;
 * doc/tasks/step5-alarms-indication.md 3.1, 3.2) — plain C, no ESP-IDF / FreeRTOS, compiled into the
 * host test as well. The pump logic is not changed by any of them: they are only shown.
 *
 *   E1   collector sensor faulty
 *   E2   tank sensor faulty
 *   E4   tank at the emergency ceiling: >= 95 °C, released below 93 °C; not with a faulty tank sensor
 *   E5   no pump start, collector at the boiling point: >= 127 °C, released below 125 °C; not with a
 *        faulty collector sensor
 *   E6   pump failure / no flow, by the rise of the two temperatures while the pump runs (below)
 *   Hot  information: the pump runs by the collector protection, or by the probe at the collector
 *        emergency temperature with a faulty tank sensor
 *
 * E6 is judged while the pump runs without a break by the normal control (the difference, its
 * minimum run time, the collector protection — not the pump test, not the anti-seize run, not with
 * a faulty sensor). The two temperatures are sampled from the start of such a run, 60 times per
 * window (10 min: every 10 s); from the moment the run is one window long, every new sample is
 * compared with the one a window older:
 *   declared: collector rose >= 10 K  and  tank rose < 0.5 K  and  collector - tank >= 20 K
 *   cleared:  tank rose >= 0.5 K  or  collector - tank < 10 K
 * Otherwise E6 stays as it is, until the restart.
 *
 * Temperatures are tenths of °C (differences: tenths of K), times are seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_pump_logic.h"

/* The codes, in the order they are shown: the alarms by number, then the information. */
typedef enum {
    SH_ALARM_E1 = 0,
    SH_ALARM_E2,
    SH_ALARM_E4,
    SH_ALARM_E5,
    SH_ALARM_E6,
    SH_ALARM_HOT,
    SH_ALARM_COUNT
} sh_alarm_t;

/* Active codes are a set of bits. */
#define SH_ALARM_BIT(code)      ((uint8_t)(1u << (code)))
#define SH_ALARMS_OVERHEAT      (SH_ALARM_BIT(SH_ALARM_E4) | SH_ALARM_BIT(SH_ALARM_E5))   /* "dangerous, now" */
#define SH_ALARMS_FAILURE       (SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_E2) | \
                                 SH_ALARM_BIT(SH_ALARM_E6))                               /* repair needed */
#define SH_ALARMS_ERROR         (SH_ALARMS_OVERHEAT | SH_ALARMS_FAILURE)                  /* every alarm */

/* E6 thresholds (Kconfig "Control") */
typedef struct {
    uint32_t window_s;          /* the pump has run this long, and the rises are over this time */
    int16_t  col_rise;          /* declared: collector rise >= this, 0.1 K */
    int16_t  tank_rise;         /* declared: tank rise < this; cleared: >= this, 0.1 K */
    int16_t  diff;              /* declared: collector - tank >= this, 0.1 K */
    int16_t  clear_diff;        /* cleared: collector - tank < this, 0.1 K */
} sh_alarm_params_t;

/* Inputs of one step: the published measurement and the pump decision of the same step. */
typedef struct {
    int16_t t_col;              /* last valid value; SH_TEMP_INVALID if none since boot */
    int16_t t_tank;
    bool col_fault;
    bool tank_fault;
    bool pump_on;
    sh_pump_reason_t pump_reason;
} sh_alarm_in_t;

#define SH_E6_SLOTS             60      /* samples per window */

/* Everything kept between the steps; all zeros at boot. */
typedef struct {
    uint8_t active;             /* SH_ALARM_BIT() of every active code: the result of the last step */
    bool tank_ceiling;          /* tank reached 95 °C, not yet below 93 °C */
    bool col_boiling;           /* collector reached the boiling point, not yet below it - 2 K */
    bool e6;
    /* E6: the samples of the current run by the normal control */
    bool run;
    uint32_t next_s;            /* next sample */
    uint8_t n;                  /* samples of this run, up to SH_E6_SLOTS + 1 */
    uint8_t head;               /* next slot; the oldest sample once the ring is full */
    int16_t col[SH_E6_SLOTS + 1];
    int16_t tank[SH_E6_SLOTS + 1];
} sh_alarms_t;

/* One step at `now_s`, after the pump decision. Returns the active codes (also in `a->active`). */
uint8_t sh_alarms_step(sh_alarms_t *a, const sh_alarm_params_t *p, const sh_alarm_in_t *in,
                       uint32_t now_s);

/* The code on the 3 digits of the display: " E1" ... " E6" (right-aligned), "Hot". */
const char *sh_alarm_text(sh_alarm_t code);

/* The same without the leading space, for the log. */
const char *sh_alarm_name(sh_alarm_t code);

/* Short English description, for the log. */
const char *sh_alarm_desc(sh_alarm_t code);

/* Normal screen of the display: one cycle is "Boil" 1 s, tank 5 s, "Col" 1 s, collector 5 s, then
 * every active code for 1 s — the alarms by number, then the information. */
#define SH_SCREEN_LABEL_MS      1000
#define SH_SCREEN_VALUE_MS      5000
#define SH_SCREEN_CODE_MS       1000

typedef enum {
    SH_SCREEN_BOIL = 0,         /* label "Boil" */
    SH_SCREEN_T_TANK,           /* tank temperature */
    SH_SCREEN_COL,              /* label "Col" */
    SH_SCREEN_T_COL,            /* collector temperature */
    SH_SCREEN_CODE,             /* `*code` */
    SH_SCREEN_END,              /* the cycle is over: start it again */
} sh_screen_t;

/* What the normal screen shows `ms` after the start of its cycle with the codes `active`. */
sh_screen_t sh_alarm_screen(uint8_t active, uint32_t ms, sh_alarm_t *code);
