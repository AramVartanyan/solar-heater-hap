/* Web interface: the data of the page and the checks of its requests
 * (doc/web-interface.md; doc/tasks/step6-installer-web.md 3.4) — plain C, no ESP-IDF / FreeRTOS,
 * compiled into the host test too. sh_web serves them over HTTP.
 *
 *   status   one JSON object with everything the page shows; temperatures in tenths of °C
 *            (null: faulty sensor or no value yet), the pressure in tenths of bar
 *   requests bodies are forms ("key=value&key=value"), values are whole numbers or short words:
 *              set    user settings by their NVS keys (tbo, c_lo, d_on, d_of, t_st — the checks
 *                     of the button menu), fro (0 / 1), installer data (fluid, glycol, press,
 *                     col_max — only with the access code)
 *              test   cmd=start | plus | minus | stop
 *                     tz (index of the time zone, sh_clock: sh_zones — only with the code)
 *              reset  what=wifi | learned | history | defaults | factory (only with the code)
 *              time   t=<seconds since 1970, UTC> — the clock of the browser
 *            the access code ("code") is the last 6 characters of the setup network name
 *   features the functions the firmware has; the page greys out every element whose function is
 *            not in the list (doc/web-interface.md 7)
 *   history  the day chart with the last daily summaries, and the time zone names, as JSON */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sh_pump_logic.h"
#include "sh_settings.h"
#include "sh_installer.h"
#include "sh_history.h"

#define SH_WEB_CODE_LEN         6
#define SH_WEB_STATUS_MAX       2048    /* a buffer of this size always holds the status */
#define SH_WEB_HISTORY_MAX      7168    /* ... the day chart with the last days */
#define SH_WEB_ZONES_MAX        2560    /* ... the time zone names */

/* Everything the page can change. */
typedef struct {
    sh_settings_t settings;     /* the menu values */
    bool frost;                 /* frost protection enabled */
    sh_installer_t inst;
    int16_t tz;                 /* time zone: index in sh_zones, SH_ZONE_NONE: not confirmed (UTC) */
} sh_web_config_t;

typedef struct {
    int16_t t_col, t_tank;      /* last valid values, SH_TEMP_INVALID: none */
    bool col_fault, tank_fault;
    bool pump_on;
    sh_pump_reason_t reason;
    uint8_t alarms;             /* SH_ALARM_BIT() of sh_alarm_t */
    bool test_on;
    uint32_t test_left_s;
    uint32_t run_s[SH_RUN_KINDS];   /* the pump today: charging, protection, other */
    bool clock_set;             /* set by SNTP or by the browser since boot */
    bool clock_ok;              /* ... and the time zone check does not doubt it */
    uint16_t clock_min;         /* local time, minute of the day */
    uint16_t peak_min;          /* collector peak of the last sunny day; SH_PEAK_NONE */
    bool peak_in;               /* it was inside 10:00 - 17:00 */
    bool history;               /* the "history" partition is there */
    sh_web_config_t cfg;
    int16_t col_protect;        /* collector protection setting, 0.1 °C (limited by the cap here) */
    const char *name;           /* device name */
    const char *version;        /* firmware version */
    const char *ssid;           /* network name, IP address, signal (dBm) */
    const char *ip;
    int rssi;
} sh_web_status_t;

/* Writes the status as JSON; returns its length, or -1 if `size` is too small. */
int sh_web_status_json(char *buf, size_t size, const sh_web_status_t *s);

typedef enum {
    SH_WEB_OK = 0,
    SH_WEB_BAD,                 /* unknown key, value out of range ... : nothing is changed */
    SH_WEB_DENIED,              /* the access code is missing or wrong */
} sh_web_res_t;

#define SH_WEB_CH_SETTINGS      0x01    /* what a "set" request changed */
#define SH_WEB_CH_FROST         0x02
#define SH_WEB_CH_INSTALLER     0x04
#define SH_WEB_CH_TZ            0x08

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
    SH_WEB_RESET_WIFI = 0,      /* the stored network */
    SH_WEB_RESET_LEARNED,       /* learned values: the state of the time zone check */
    SH_WEB_RESET_HISTORY,       /* the daily summaries */
    SH_WEB_RESET_DEFAULTS,      /* user and installer settings */
    SH_WEB_RESET_FACTORY,       /* everything */
} sh_web_reset_t;

/* "reset": needs the access code. */
sh_web_res_t sh_web_reset(const char *body, const char *code, sh_web_reset_t *what);

/* Key of a pump reason for the page (it holds the texts). */
const char *sh_web_reason_key(sh_pump_reason_t reason);

/* "time": the clock of the browser, seconds since 1970 (UTC). No access code. */
sh_web_res_t sh_web_time(const char *body, uint32_t *epoch_s);

/* The day chart ("day": 1 — points of the local day, x = minute of the day; 0 — of the last 24 h,
 * x = minutes ago; a point: [x, collector, tank, charging s, protection s, other s]) and the last
 * daily summaries, the newest first. The length, or -1 if `size` is too small. */
int sh_web_history_json(char *buf, size_t size, const sh_hist_point_t *pts, int n, bool day,
                        const sh_hist_day_t *days, int n_days);

/* The names of the time zones, in the order of sh_zones. */
int sh_web_zones_json(char *buf, size_t size);
