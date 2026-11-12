/* Status LED indication: state -> (colour, pattern) (doc/led-indication.md 1 - 5;
 * doc/tasks/step5-alarms-indication.md 3.3) — plain C, no ESP-IDF / FreeRTOS, compiled into the
 * host test as well. sh_led drives the LED.
 *
 * Colour, by priority (first match wins):
 *   red      any alarm (E1, E2, E4, E5, E6, E8) — blinking only
 *   orange   the pump runs because of a protection (Hot, Fro) or cools the tank at night
 *   purple   vacation
 *   yellow   collector > 90 °C (released below 88 °C), pump stopped
 *   green    the pump runs (any other reason: normal charging, test, anti-seize, sensor-fault modes)
 *   white    collector < 20 °C (released above 22 °C)
 *   blue     idle
 * Pattern: red -> fast (E4, E5; also with E1 / E2 / E6 / E8 at the same time) or double (E1, E2, E6, E8);
 * other colours -> steady with Wi-Fi, heartbeat without it (or while the setup portal is open).
 * Temperatures are tenths of °C, times ms. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_IND_RETURN_ZONE      20      /* 2 K */

/* Pattern times, ms */
#define SH_IND_FAST_ON_MS       150u    /* fast: 150 on / 150 off */
#define SH_IND_FAST_OFF_MS      150u
#define SH_IND_DOUBLE_ON_MS     100u    /* double: 100 on, 100 off, 100 on, then 1 s dark */
#define SH_IND_DOUBLE_GAP_MS    100u
#define SH_IND_DOUBLE_DARK_MS   1000u
#define SH_IND_BEAT_ON_MS       80u     /* heartbeat: 80 ms on every 2 s */
#define SH_IND_BEAT_PERIOD_MS   2000u

typedef enum {
    SH_COLOR_OFF = 0,
    SH_COLOR_BLUE,      /* idle */
    SH_COLOR_GREEN,     /* pump running */
    SH_COLOR_WHITE,     /* cold collector / OTA */
    SH_COLOR_YELLOW,    /* hot collector, pump stopped */
    SH_COLOR_ORANGE,    /* pump running because of a protection */
    SH_COLOR_PURPLE,    /* vacation */
    SH_COLOR_RED,       /* alarm (blinking only) */
    SH_COLOR_COUNT
} sh_color_t;

typedef enum {
    SH_IND_STEADY = 0,  /* Wi-Fi connected */
    SH_IND_HEARTBEAT,   /* no Wi-Fi / setup portal */
    SH_IND_FAST,        /* E4, E5 */
    SH_IND_DOUBLE,      /* E1, E2, E6, E8 */
} sh_ind_pattern_t;

typedef struct {
    int16_t cold;       /* white below this, 0.1 °C */
    int16_t hot;        /* yellow above this, 0.1 °C */
} sh_ind_params_t;

typedef struct {
    uint8_t alarms;     /* SH_ALARM_BIT() of sh_alarm_t */
    bool pump_on;
    int16_t t_col;      /* SH_TEMP_INVALID: none */
    bool col_fault;
    bool vacation;
    bool cooling;       /* the pump runs by the night cooling */
    bool wifi_connected;
    bool wifi_portal;
} sh_ind_in_t;

/* The memory of the white / yellow return zones; all zeros at boot. */
typedef struct {
    bool cold;
    bool hot;
} sh_ind_state_t;

typedef struct {
    sh_color_t color;
    sh_ind_pattern_t pattern;
} sh_ind_out_t;

/* The indication for `in`; updates the return zones in `st`. Called when the inputs change. */
sh_ind_out_t sh_ind_update(sh_ind_state_t *st, const sh_ind_params_t *p, const sh_ind_in_t *in);

/* The LED is lit `ms` after the start of `pattern`. */
bool sh_ind_lit(sh_ind_pattern_t pattern, uint32_t ms);
