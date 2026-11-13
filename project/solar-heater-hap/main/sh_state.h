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
    /* group D: the heater, its leak, E8 */
    bool    heater_on;          /* the heater sign is active now */
    bool    heater_det;         /* the heater was detected with "0 kW" */
    uint8_t leak;               /* sh_leak_phase_t: 0 none, 1 check, 2 stopped */
    uint32_t leak_start_s;      /* the start of the running check, monotonic s */
    bool    leak_last;          /* a check has been made since boot ... */
    bool    leak_last_stop;     /* ... its result: stopped */
    int16_t leak_last_drop;     /* ... the collector fell by, 0.1 K */
    int16_t e8_normal;          /* learned normal night cooling of the tank, 0.1 K/h, INT16_MIN: none */
    /* group D2: the pulse mode (the sensors on the pipes) */
    bool    pulse;              /* the pulse mode is on */
    bool    rel_any;            /* there are reliable values ... */
    int16_t rel_col, rel_tank;  /* ... (SH_TEMP_INVALID: none / faulty) */
    uint32_t rel_s;             /* ... taken then, monotonic s */
    bool    rel_pulse;          /* ... at a pulse (otherwise during a longer run) */
    bool    rel_now;            /* ... taken now: the pump runs for >= 30 s */
    uint32_t pulse_next_s;      /* the next pulse, monotonic s; 0: none planned */
    bool    pulse_hot;          /* no pulses after an overheated collector (a warning) */
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
    bool    ota_running;        /* a firmware update is being written */
    bool    net_ap;             /* the network mode in use is AP (the own access point) */
    bool    fw_prompt;          /* "FUp" asks for the confirmation of a firmware update */
    bool    fw_wait;            /* a confirmed firmware update waits for its start */
    uint8_t alarms;             /* active codes: SH_ALARM_BIT() of sh_alarm_t (sh_alarm.h) */
    sh_modes_t modes;           /* season, night cooling, air venting */
} sh_state_t;

void sh_state_init(void);
void sh_state_get(sh_state_t *out);                 /* consistent copy */
/* The same with the temperatures to be shown (display, web interface, chart, history, LED): in the
 * pulse mode the last reliable values instead of the pipes. */
void sh_state_get_shown(sh_state_t *out);
void sh_state_set_temps(int16_t t_col, int16_t t_tank, bool col_fault, bool tank_fault);
void sh_state_set_pump(bool on, sh_pump_reason_t reason);
void sh_state_set_test(bool on, uint32_t start_s, uint32_t end_s);
void sh_state_set_wifi(bool connected, bool portal);
void sh_state_set_alarms(uint8_t alarms);
void sh_state_set_vacation(bool on);
void sh_state_set_modes(const sh_modes_t *modes);
/* The network mode and the firmware update (sh_link). */
void sh_state_set_link(bool net_ap, bool fw_prompt, bool fw_wait, bool ota_running);
