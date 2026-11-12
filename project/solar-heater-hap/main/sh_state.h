/* Shared state: the single place where modules publish and read values. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_pump_logic.h"

#define SH_TEMP_INVALID     (-1000)     /* tenths of °C: sensor missing or read error */

/* Season, night cooling, sunrise / sunset and air venting, published by the control task. */
typedef struct {
    bool    winter;             /* the current season */
    uint8_t cool;               /* night cooling session: sh_cool_trigger_t, 0 none */
    int16_t cool_target;        /* its target (triggers a, b), 0.1 °C */
    bool    cool_clock;         /* it ends by the clock (07:00), otherwise at the sunrise */
    bool    cool_heater;        /* stopped by the heater until the next night */
    int16_t cool_learned;       /* the target with the learned correction, 0.1 °C */
    int16_t sunrise, sunset;    /* learned averages, local minutes, -1: none */
    uint16_t sunrises, sunsets; /* detected since boot (counters, for the daily summary) */
    bool    air_on;             /* air venting running */
    uint32_t air_start_s;       /* its start, monotonic s */
    uint32_t air_pause_s;       /* the pause of its cycles, s */
} sh_modes_t;

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
    sh_modes_t modes;           /* season, night cooling, air venting */
} sh_state_t;

void sh_state_init(void);
void sh_state_get(sh_state_t *out);                 /* consistent copy */
void sh_state_set_temps(int16_t t_col, int16_t t_tank, bool col_fault, bool tank_fault);
void sh_state_set_pump(bool on, sh_pump_reason_t reason);
void sh_state_set_test(bool on, uint32_t start_s, uint32_t end_s);
void sh_state_set_wifi(bool connected, bool portal);
void sh_state_set_alarms(uint8_t alarms);
void sh_state_set_vacation(bool on);
void sh_state_set_modes(const sh_modes_t *modes);
