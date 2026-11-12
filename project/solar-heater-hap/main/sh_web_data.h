/* Web interface data (doc/web-interface.md; doc/tasks/step6-installer-web.md 3.4) — plain C, no
 * ESP-IDF, compiled into the host test as well. sh_web serves the page and calls these.
 *
 *   GET  /api/state   the JSON of sh_web_state_json(): the available features, codes, temperatures,
 *                     pump, settings with their ranges, installer data, computed limits, network
 *   POST /api/set     k=<key>&v=<integer>: a setting (tbo, c_lo, d_on, d_of, t_st, frost — free)
 *                     or installer data (coolant, glycol, p_min, col_max — with the code)
 *   POST /api/cmd     c=<command>: test_plus, test_minus, test_stop (free);
 *                     reset_wifi, reset_settings, reset_factory (with the code)
 *   POST /api/code    code=<code>: checks the code (the page remembers it in a cookie "code")
 *
 * Values are integers as stored: tenths of °C for temperatures, tenths of bar for the pressure.
 * The access code is the last 6 characters of the device's own network name ("SOLAR-810841" ->
 * "810841"), compared without regard to case.
 *
 * Features: the firmware lists the ones it has (sh_web_features); every element of the page
 * carries the name of its feature, the page greys out the elements whose feature is missing. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sh_pump_logic.h"
#include "sh_settings.h"
#include "sh_installer.h"

#define SH_WEB_CODE_LEN     6

/* Everything the page shows, taken at one moment. */
typedef struct {
    int16_t t_col;              /* last valid value; SH_TEMP_INVALID: none */
    int16_t t_tank;
    bool col_fault;
    bool tank_fault;
    bool pump_on;
    sh_pump_reason_t reason;
    bool test_on;
    uint32_t test_left_s;
    uint8_t alarms;             /* SH_ALARM_BIT() of sh_alarm_t */
    sh_settings_t settings;
    bool frost_on;
    sh_installer_t inst;
    sh_limits_t limits;
    int16_t col_protect;        /* setting, 0.1 °C */
    int16_t col_protect_used;   /* capped */
    const char *version;
    const char *host;
    const char *ssid;           /* network of the router ("" if unknown) */
    const char *ip;
    int rssi;                   /* dBm */
} sh_web_snap_t;

/* The JSON of GET /api/state into `buf`; returns its length, 0 if it does not fit. */
size_t sh_web_state_json(const sh_web_snap_t *s, char *buf, size_t size);

/* The features the firmware has, NULL-terminated. */
extern const char *const sh_web_features[];

typedef enum {
    SH_WEB_PARAM_SETTING = 0,   /* sh_settings_set(id) */
    SH_WEB_PARAM_FROST,         /* sh_settings_frost_set() */
    SH_WEB_PARAM_INSTALLER,     /* sh_installer_set(id) */
} sh_web_param_group_t;

typedef struct {
    sh_web_param_group_t group;
    int id;                     /* sh_setting_t / sh_inst_id_t */
    bool code;                  /* the change needs the access code */
} sh_web_param_t;

/* The parameter named `key` (POST /api/set); false if there is none. */
bool sh_web_param_find(const char *key, sh_web_param_t *out);

typedef enum {
    SH_WEB_CMD_TEST_PLUS = 0,   /* start the test or +60 s, as "+" */
    SH_WEB_CMD_TEST_MINUS,      /* -60 s, as "-" */
    SH_WEB_CMD_TEST_STOP,       /* stop, not earlier than 1 min after the start, as "menu" */
    SH_WEB_CMD_RESET_WIFI,      /* the stored network, restart into the setup portal */
    SH_WEB_CMD_RESET_SETTINGS,  /* the user settings and the installer data to the defaults */
    SH_WEB_CMD_RESET_FACTORY,   /* everything, restart */
    SH_WEB_CMD_COUNT
} sh_web_cmd_t;

/* The command named `name` (POST /api/cmd) and whether it needs the code; false if there is none. */
bool sh_web_cmd_find(const char *name, sh_web_cmd_t *cmd, bool *code);

/* A whole number, optional "-", nothing else, within int16_t. */
bool sh_web_parse_int(const char *s, int16_t *out);

/* `given` is the access code of the device whose own network is `ap_ssid`. */
bool sh_web_code_ok(const char *given, const char *ap_ssid);

/* Short key of the pump reason, for the texts of the page ("diff", "frost" ...). */
const char *sh_web_reason_key(sh_pump_reason_t reason);
