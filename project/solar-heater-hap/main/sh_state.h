/* Shared state: the single place where modules publish and read values. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_pump_logic.h"

#define SH_TEMP_INVALID     (-1000)     /* tenths of °C: no valid value yet */

typedef struct {
    int16_t t_col;              /* collector, 0.1 °C: the last valid value */
    int16_t t_tank;             /* tank, 0.1 °C: the last valid value */
    bool    col_fault;          /* collector sensor declared faulty (5 failed measurements) */
    bool    tank_fault;         /* tank sensor declared faulty */
    bool    pump_on;            /* relay commanded on */
    sh_pump_reason_t pump_reason;   /* the rule behind pump_on */
    bool    test_active;        /* pump test running */
    uint16_t test_left_s;       /* until the test stops the pump */
    bool    wifi_connected;
    bool    wifi_portal;        /* captive-wifi setup portal active */
    bool    hap_pair_window;    /* HomeKit pairing window open */
    bool    vacation;
    bool    ota_running;
    uint8_t alarm;              /* 0 = none, 1 = overheat, 2 = pump failure */
} sh_state_t;

void sh_state_init(void);
void sh_state_get(sh_state_t *out);                 /* consistent copy */
void sh_state_set_temps(int16_t t_col, int16_t t_tank, bool col_fault, bool tank_fault);
void sh_state_set_pump(bool on, sh_pump_reason_t reason);
void sh_state_set_test(bool active, uint16_t left_s);
void sh_state_set_wifi(bool connected, bool portal);
