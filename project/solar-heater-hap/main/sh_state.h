/* Shared state: the single place where modules publish and read values. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_pump_logic.h"

#define SH_TEMP_INVALID     (-1000)     /* tenths of °C: sensor missing or read error */

/* The modes of group C (sh_modes), published by the control task. */
typedef struct {
    bool winter;                /* the season in use */
    uint8_t cool_why;           /* sh_cool_why_t of the running session, 0: none */
    int16_t cool_goal;          /* its target, 0.1 °C; SH_TEMP_INVALID: equalisation (vacation) */
    int16_t cool_until;         /* its end by the clock (local minute), -1: the sunrise */
    bool cool_heater;           /* stopped by a heater until the next night */
    int16_t cool_target;        /* the target of a) / b) in use (with the learned correction), 0.1 °C */
    int16_t cool_learned;       /* the learned correction, 0.1 K */
    int16_t sun_rise, sun_set;  /* detected today (local minutes), -1: none */
    int16_t avg_rise, avg_set;  /* averages of the last 7 days, -1: none */
    bool air;                   /* air venting running */
    uint32_t air_start_s;       /* its start, monotonic s */
    uint8_t air_cycle;          /* 1 ... 3 */
    bool air_run;               /* run (or pause) */
    uint32_t air_left_s;        /* of the whole venting */
} sh_state_modes_t;

typedef struct {
    int16_t t_col;              /* collector, 0.1 °C: last valid value (SH_TEMP_INVALID: none yet) */
    int16_t t_tank;             /* tank, 0.1 °C: likewise */
    bool    col_fault;          /* collector sensor faulty (5 failed measurements in a row) */
    bool    tank_fault;         /* tank sensor faulty */
    bool    pump_on;            /* relay commanded on */
    sh_pump_reason_t pump_reason;   /* the rule behind pump_on */
    bool    test_on;            /* pump test running */
    uint32_t test_start_s;      /* its start, monotonic s */
    uint32_t test_end_s;        /* its end, monotonic s */
    bool    wifi_connected;
    bool    wifi_portal;        /* captive-wifi setup portal active */
    bool    hap_pair_window;    /* HomeKit pairing window open */
    bool    vacation;
    bool    ota_running;
    uint8_t alarms;             /* active codes: SH_ALARM_BIT() of sh_alarm_t (sh_alarm.h) */
    sh_state_modes_t modes;
} sh_state_t;

void sh_state_init(void);
void sh_state_get(sh_state_t *out);                 /* consistent copy */
void sh_state_set_temps(int16_t t_col, int16_t t_tank, bool col_fault, bool tank_fault);
void sh_state_set_pump(bool on, sh_pump_reason_t reason);
void sh_state_set_test(bool on, uint32_t start_s, uint32_t end_s);
void sh_state_set_wifi(bool connected, bool portal);
void sh_state_set_alarms(uint8_t alarms);
void sh_state_set_modes(bool vacation, const sh_state_modes_t *modes);
