/* Alarm and information codes (doc/operating-modes.md 2.1, p. 13; doc/led-indication.md 6;
 * doc/tasks/step5-alarms-indication.md 3.1) — plain C, no ESP-IDF / FreeRTOS, compiled into the
 * host test as well.
 *
 * Temperatures are tenths of °C, times are seconds of a monotonic clock.
 *
 *   E1   collector sensor faulty
 *   E2   tank sensor faulty
 *   E4   tank at the emergency ceiling (>= 95 °C, released below 93 °C); the tank sensor is not faulty
 *   E5   collector at the boiling point (computed from the installer data, released below it - 2 K);
 *        its sensor is not faulty
 *   E6   pump failure / no flow: the pump runs continuously by the normal control (difference,
 *        minimum run time, collector protection; no sensor faulty). The first `e6_time_s` of the run
 *        are skipped; then, once a minute, over the last `e6_time_s`:
 *          declared when the collector rose >= e6_col_rise, the tank rose < e6_tank_rise and the
 *          difference collector - tank is >= e6_diff;
 *          cleared when the tank rose >= e6_tank_rise or the difference is < e6_clear_diff.
 *        Otherwise it stays until restart (the pump logic is not changed: E6 is only shown).
 *   Hot  (information) the pump runs by the collector protection, or by the probe at the collector
 *        emergency temperature while the tank sensor is faulty.
 *   Fro  (information) the pump runs by the frost protection.
 * E8 (A8) follows in a later step. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_pump_logic.h"

/* In the order of the display: the alarms by number, then the information. */
typedef enum {
    SH_ALARM_E1 = 0,
    SH_ALARM_E2,
    SH_ALARM_E4,
    SH_ALARM_E5,
    SH_ALARM_E6,
    SH_ALARM_HOT,
    SH_ALARM_FRO,
    SH_ALARM_COUNT              /* also "no code" */
} sh_alarm_t;

#define SH_ALARM_BIT(a)         (1u << (a))
#define SH_ALARM_OVERHEAT       (SH_ALARM_BIT(SH_ALARM_E4) | SH_ALARM_BIT(SH_ALARM_E5))
#define SH_ALARM_REPAIR         (SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_E2) | \
                                 SH_ALARM_BIT(SH_ALARM_E6))
#define SH_ALARM_PROTECT        (SH_ALARM_BIT(SH_ALARM_HOT) | SH_ALARM_BIT(SH_ALARM_FRO))
#define SH_ALARM_CODE_MS        1000u   /* each code on the display */
#define SH_ALARM_E6_MAX_MIN     30u     /* longest E6 time (Kconfig range) */

typedef struct {
    uint32_t e6_time_s;         /* skipped run time and window, a multiple of 60 s, <= 30 min */
    int16_t e6_col_rise;        /* 0.1 K */
    int16_t e6_tank_rise;       /* 0.1 K */
    int16_t e6_diff;            /* 0.1 K */
    int16_t e6_clear_diff;      /* 0.1 K */
} sh_alarm_params_t;

/* Inputs of one step. */
typedef struct {
    int16_t t_col;              /* last valid value; SH_TEMP_INVALID if none since boot */
    int16_t t_tank;
    bool col_fault;
    bool tank_fault;
    bool pump_on;
    sh_pump_reason_t reason;
    bool col_boiling;           /* the limits of sh_pump_ctl (norm.col_boiling, norm.tank_ceiling) */
    bool tank_ceiling;
} sh_alarm_in_t;

/* Kept between the steps; all zeros at boot. */
typedef struct {
    uint8_t active;             /* SH_ALARM_BIT() of the active codes */
    bool e6;
    bool run;                   /* the pump runs by the normal control */
    uint32_t next_sample_s;     /* next E6 sample (one a minute after the skipped time) */
    int16_t col[SH_ALARM_E6_MAX_MIN + 1];
    int16_t tank[SH_ALARM_E6_MAX_MIN + 1];
    uint8_t n;                  /* samples of this run, up to the window + 1 */
    uint8_t head;               /* next slot */
} sh_alarm_state_t;

/* One step at `now_s`; returns the active codes (also in `st->active`). */
uint8_t sh_alarm_step(sh_alarm_state_t *st, const sh_alarm_params_t *p, const sh_alarm_in_t *in,
                      uint32_t now_s);

/* Display: the code shown `ms` after the end of the standard cycle (each code 1 s, in the order of
 * sh_alarm_t); SH_ALARM_COUNT when the codes are over. */
sh_alarm_t sh_alarm_code_at(uint8_t active, uint32_t ms);

/* How many codes are active (the vacation label "HoL" follows them on the display). */
unsigned sh_alarm_count(uint8_t active);

/* "E1" ... "Hot", "Fro", for the log. */
const char *sh_alarm_name(sh_alarm_t a);
