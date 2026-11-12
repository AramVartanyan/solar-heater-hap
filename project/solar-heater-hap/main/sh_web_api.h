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
 *              reset  what=wifi | defaults | factory (only with the access code)
 *            the access code ("code") is the last 6 characters of the setup network name
 *   features the functions the firmware has; the page greys out every element whose function is
 *            not in the list (doc/web-interface.md 7) */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sh_pump_logic.h"
#include "sh_settings.h"
#include "sh_installer.h"

#define SH_WEB_CODE_LEN         6
#define SH_WEB_STATUS_MAX       2048    /* a buffer of this size always holds the status */

/* Everything the page can change. */
typedef struct {
    sh_settings_t settings;     /* the menu values */
    bool frost;                 /* frost protection enabled */
    sh_installer_t inst;
} sh_web_config_t;

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
    SH_WEB_RESET_DEFAULTS,      /* user and installer settings */
    SH_WEB_RESET_FACTORY,       /* everything */
} sh_web_reset_t;

/* "reset": needs the access code. */
sh_web_res_t sh_web_reset(const char *body, const char *code, sh_web_reset_t *what);

/* Key of a pump reason for the page (it holds the texts). */
const char *sh_web_reason_key(sh_pump_reason_t reason);
