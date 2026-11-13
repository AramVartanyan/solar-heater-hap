/* Created by Aram Vartanyan (c) 2026 */

/* Web interface: the data of the page and the checks of its requests
 * (doc/web-interface.md) — plain C, no ESP-IDF / FreeRTOS,
 * compiled into the host test too. sh_web serves them over HTTP.
 *
 *   status   one JSON object with everything the page shows; temperatures in tenths of °C
 *            (null: faulty sensor or no value yet), the pressure in tenths of bar; in the pulse
 *            mode the temperatures are the last reliable values and "pulse" says when they were
 *            taken and when the next pulse comes
 *   requests bodies are forms ("key=value&key=value"), values are whole numbers or short words:
 *              set    user settings by their NVS keys (tbo, c_lo, d_on, d_of, t_st and the winter
 *                     set c_lo_w, d_on_w, d_of_w — the checks of the button menu; ssn 0 / 1 / 2
 *                     season, hol 0 / 1 vacation, nc / nc_w 0 / 1 night cooling above Tmax in
 *                     summer / winter), fro (0 / 1), installer data (fluid, glycol, press,
 *                     col_max, col_type, col_area, tank_vol, heater, mount — only with the access
 *                     code)
 *              test   cmd=start | plus | minus | stop
 *              air    cmd=start | stop: the air venting
 *              reset  what=wifi | homekit | defaults | factory | history | learned (only with the
 *                     access code)
 *              hap    on=0 | 1: the setting "Apple HomeKit" (with the code; off is refused while a
 *                     controller is paired)
 *              lock   on=0 | 1: the keyboard lock (with the code)
 *              time   t=<s since 1970, UTC>: the time of the browser (no code; sh_clock decides
 *                     whether it is taken)
 *              tz     tz=<index in sh_tz_list>: the installer confirms a time zone (with the code)
 *              ota    cmd=check | update: the firmware update (with the code)
 *            the access code ("code") is the last 6 characters of the setup network name
 *   chart    the day in RAM (sh_day): the 10-min intervals of today / of the last 24 h
 *   days     the last daily summaries (sh_hist), newest first
 *   features the functions the firmware has; the page greys out every element whose function is
 *            not in the list (doc/web-interface.md 7); "history" only with the history partition */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sh_pump_logic.h"
#include "sh_settings.h"
#include "sh_installer.h"
#include "sh_clock_rules.h"
#include "sh_day.h"
#include "sh_hist.h"
#include "sh_net_rules.h"
#include "sh_ota.h"

#define SH_WEB_CODE_LEN         6
#define SH_WEB_STATUS_MAX       3072    /* a buffer of this size always holds the status */

/* Everything the page can change. */
typedef struct {
    sh_settings_t settings;     /* the menu values */
    bool frost;                 /* frost protection enabled */
    sh_installer_t inst;
} sh_web_config_t;

/* The clock, as the page shows it. */
typedef struct {
    sh_clock_trust_t trust;
    bool sntp;                  /* set by SNTP (otherwise by the browser, or not set) */
    int16_t min;                /* local minute of the day, -1: not set */
    const char *tz;             /* time zone name */
    bool tz_confirmed;
    int16_t peak;               /* collector peak of the last counted day, SH_PEAK_NONE */
} sh_web_clock_t;

typedef struct {
    int16_t t_col, t_tank;      /* last valid values, SH_TEMP_INVALID: none */
    bool col_fault, tank_fault;
    bool pump_on;
    sh_pump_reason_t reason;
    uint8_t alarms;             /* SH_ALARM_BIT() of sh_alarm_t */
    bool test_on;
    uint32_t test_left_s;
    sh_web_config_t cfg;
    int16_t col_protect;        /* collector protection setting, 0.1 °C (limited by the cap here) */
    const char *name;           /* device name */
    const char *version;        /* firmware version */
    const char *serial;         /* serial number: the base MAC, 12 hex digits in capitals (its last 6, in lower case, are the access code) */
    const char *ssid;           /* network name, IP address, signal (dBm) */
    const char *ip;
    int rssi;
    /* the network mode and the name */
    bool ap;                    /* the own access point is on: `ip` is its address, no `ssid` */
    bool ap_pending;            /* AP chosen during an update: it starts after it */
    const char *host;           /* the name in the network: "Solar-810840" (mDNS: + ".local") */
    const char *ap_ssid;        /* the name of the own access point: the same text */
    /* the firmware update */
    sh_ota_phase_t ota;
    int8_t ota_pct;
    const char *ota_version;    /* the newer version, "" / NULL: none */
    /* Apple HomeKit */
    bool hap_on;                /* the setting */
    bool hap_paired;            /* a controller is paired */
    bool hap_running;           /* HAP is running (connected to the router since boot) */
    bool hap_window;            /* the pairing window is open */
    bool key_lock;              /* the keyboard is locked */
    bool fahrenheit;            /* the display units chosen in HomeKit: the page shows °F (the data stay °C) */
    sh_web_clock_t clock;
    uint32_t run_s[SH_RUN_KINDS];   /* run time today by kind (sh_day_run_today) */
    bool history;               /* the history partition is there */
    bool winter;                /* the current season */
    /* night cooling */
    uint8_t cool;               /* the active session: sh_cool_trigger_t, 0 none */
    int16_t cool_target;        /* of the session (not in the vacation), 0.1 °C */
    int16_t cool_until;         /* local minute (07:00), -1: until the sunrise */
    bool cool_heater;           /* stopped by the heater until the next night */
    int16_t cool_learned;       /* the target with the learned correction, 0.1 °C */
    int16_t sunrise, sunset;    /* learned averages, local minutes, -1: none */
    /* air venting */
    bool air_on, air_run;
    uint8_t air_cycle;
    uint32_t air_left_s;
    /* the heater: the sign now, detected with "0 kW", the leak checks, E8 learning */
    bool heater_on, heater_det;
    uint8_t leak;               /* sh_leak_phase_t */
    uint32_t leak_left_s;       /* of the running check */
    bool leak_last, leak_last_stop;
    int16_t leak_last_drop;     /* 0.1 K */
    int16_t e8_normal;          /* learned normal night cooling, 0.1 K/h, INT16_MIN: none */
    /* the pulse mode: t_col / t_tank are the last reliable values */
    bool pulse;
    bool meas;                  /* they exist ... */
    uint32_t meas_age_s;        /* ... taken this long ago ... */
    bool meas_pulse;            /* ... at a pulse (otherwise during a longer run) */
    bool meas_now;              /* ... now: the pump runs for >= 30 s */
    bool pulse_next;            /* a next pulse is planned ... */
    uint32_t pulse_left_s;      /* ... in */
    bool pulse_hot;             /* no pulses after an overheated collector */
} sh_web_status_t;

/* Writes the status as JSON; returns its length, or -1 if `size` is too small. */
int sh_web_status_json(char *buf, size_t size, const sh_web_status_t *s);

/* The chart as JSON: {"day":1,"pts":[[x,col,tank,charge,protect,other,cool],...]} — x is the end of
 * the interval in minutes: with a set clock from midnight (10 ... 1440), without it from now
 * (-1440 ... 0); temperatures in tenths (null: none), run time in whole minutes. */
#define SH_WEB_CHART_MAX        7168
int sh_web_chart_json(char *buf, size_t size, const sh_day_t *d, const sh_local_t *lt, uint32_t now_s);

/* The last daily summaries, newest first: {"on":1,"days":[{"d":"2026-11-11","cx":..,"cn":..,
 * "tx":..,"tn":..,"run":[c,p,o],"cool":..,"heat":..,"al":["E1"],"pk":..,"rel":1,"part":0},...]};
 * `on`: the history partition is there. */
#define SH_WEB_DAYS             7
#define SH_WEB_DAYS_MAX         2048
int sh_web_days_json(char *buf, size_t size, bool on, const sh_hist_rec_t *days, unsigned n);

/* The supported time zones: {"tz":["UTC","Europe/Sofia",...]} (the index is sent back). */
#define SH_WEB_TZ_MAX           2048
int sh_web_tz_json(char *buf, size_t size);

typedef enum {
    SH_WEB_OK = 0,
    SH_WEB_BAD,                 /* unknown key, value out of range ... : nothing is changed */
    SH_WEB_DENIED,              /* the access code is missing or wrong */
} sh_web_res_t;

#define SH_WEB_CH_SETTINGS      0x01    /* what a "set" request changed */
#define SH_WEB_CH_FROST         0x02
#define SH_WEB_CH_INSTALLER     0x04

/* "set": applies `body` to `cfg` (the current values). The pairs are taken in their order, each
 * menu value against the range the others leave it (as in the menu). `code` is the device access
 * code. `cfg` and `changed` are valid only with SH_WEB_OK. */
sh_web_res_t sh_web_set(const char *body, const char *code, sh_web_config_t *cfg, unsigned *changed);

/* The body carries the right access code. */
bool sh_web_code_ok(const char *body, const char *code);

typedef enum {
    SH_WEB_TEST_NONE = 0,       /* nothing to do (e.g. "start" while the test runs) */
    SH_WEB_TEST_PLUS,           /* start, or +60 s */
    SH_WEB_TEST_MINUS,
    SH_WEB_TEST_STOP,
} sh_web_test_t;

/* "test": the command for the control task, as the buttons give it. */
sh_web_res_t sh_web_test(const char *body, bool test_on, sh_web_test_t *cmd);

typedef enum {
    SH_WEB_AIR_NONE = 0,        /* nothing to do (e.g. "start" while it or the pump test runs) */
    SH_WEB_AIR_START,
    SH_WEB_AIR_STOP,
} sh_web_air_t;

/* "air": the air venting command for the control task. */
sh_web_res_t sh_web_air(const char *body, bool air_on, bool test_on, sh_web_air_t *cmd);

typedef enum {
    SH_WEB_RESET_WIFI = 0,      /* the stored network */
    SH_WEB_RESET_DEFAULTS,      /* user and installer settings */
    SH_WEB_RESET_FACTORY,       /* everything */
    SH_WEB_RESET_HISTORY,       /* the daily summaries */
    SH_WEB_RESET_LEARNED,       /* the learned values: sunrise / sunset, the cooling target, the zone check */
    SH_WEB_RESET_HOMEKIT,       /* the HomeKit pairings */
} sh_web_reset_t;

/* "reset": needs the access code. */
sh_web_res_t sh_web_reset(const char *body, const char *code, sh_web_reset_t *what);

/* "time": the time of the browser, s since 1970 (UTC). */
sh_web_res_t sh_web_time(const char *body, int64_t *epoch_s);

/* "tz": a time zone of sh_tz_list by its index; needs the access code. */
sh_web_res_t sh_web_tz(const char *body, const char *code, int *tz);

/* "ota": check for a newer firmware, or install the found one; needs the access code. */
sh_web_res_t sh_web_ota(const char *body, const char *code, sh_ota_cmd_t *cmd);

/* "hap": the setting "Apple HomeKit"; needs the access code. `on_now`, `paired`: the setting and
 * the pairing now — switching off while paired is refused (SH_WEB_BAD), as in the menu. */
sh_web_res_t sh_web_hap(const char *body, const char *code, bool on_now, bool paired, bool *on);

/* "lock": the keyboard lock (Lock Physical Controls); needs the access code. */
sh_web_res_t sh_web_lock(const char *body, const char *code, bool *on);

/* Key of the firmware update state for the page (it holds the texts): "ap" (no internet: own
 * access point) instead of everything that needs the server, "noconn" for a failed check. */
const char *sh_web_ota_key(sh_ota_phase_t phase, bool ap);

/* Key of a pump reason for the page (it holds the texts). */
const char *sh_web_reason_key(sh_pump_reason_t reason);
