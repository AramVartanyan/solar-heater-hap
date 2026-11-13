/* Apple HomeKit: what the accessory shows and takes (doc/operating-modes.md 3;
 * doc/tasks/step14-homekit.md) — plain C, no ESP-IDF / FreeRTOS / esp-homekit-sdk, compiled into the
 * host test too. sh_hap builds the accessory with esp-homekit-sdk and feeds these rules.
 *
 *   Heater Cooler (primary)
 *     Active                      1 normal mode, 0 = the vacation (and back: the vacation of the
 *                                 menu / the web interface -> Active)
 *     Current Heater Cooler State Idle: the pump stands; Heating: it runs (charging the tank);
 *                                 Cooling: it runs by the night cooling
 *     Target Heater Cooler State  Heat only
 *     Current Temperature         the tank, 0 ... 100 °C
 *     Heating Threshold           Tmax, 30 ... 85 °C, step 1
 *     Temperature Display Units   set by HomeKit only (kept in NVS, °C without a choice): what the
 *                                 user sees — the display (the temperatures and the menu values)
 *                                 and the web interface — is shown in °F; everything inside stays
 *                                 in °C / K: the measurements, the logic, the settings in NVS, the
 *                                 history and its CSV, and the values sent to HomeKit
 *     Lock Physical Controls      the keyboard lock: one variable, changed by the buttons ("+" and
 *                                 "-" held 3 s, the menu position LoC), by Apple Home and by the
 *                                 web interface — each undoes the others; kept in NVS. Locked, no
 *                                 button does anything (sh_menu); the web interface is not locked
 *   "Collector" / "Tank"          Temperature Sensor with Status Fault: Collector — E1, E5 (-40 ...
 *                                 200 °C), Tank — E2, E4 (0 ... 100 °C)
 * In the pulse mode the temperatures are the last reliable values (the caller passes them).
 * The temperatures are answered when a controller reads them and are never sent on their own; a
 * notification goes out when one of these changes: Active, Current State, Heating Threshold, Lock
 * Physical Controls, Status Fault (either sensor).
 *
 * HAP runs only with the setting "Apple HomeKit" on (default) and only once the device is connected
 * to the router; it is never stopped again. The setting changes with a restart only, and it cannot
 * be switched off while a controller is paired. With HAP running the web interface is on its HTTP
 * server; otherwise on the own one.
 * Temperatures are tenths of °C. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_pump_logic.h"
#include "sh_settings.h"

/* Current Heater Cooler State (HAP: 0 Inactive is not used) */
typedef enum {
    SH_HAP_IDLE = 1,
    SH_HAP_HEATING = 2,
    SH_HAP_COOLING = 3,
} sh_hap_cur_t;

#define SH_HAP_TARGET_HEAT      1       /* Target Heater Cooler State: the only value */
#define SH_HAP_UNITS_C          0       /* Temperature Display Units */
#define SH_HAP_UNITS_F          1

/* The ranges of the temperatures in HomeKit, 0.1 °C */
#define SH_HAP_TANK_MIN         0
#define SH_HAP_TANK_MAX         1000
#define SH_HAP_COL_MIN          (-400)
#define SH_HAP_COL_MAX          2000

/* What the logic has now. */
typedef struct {
    bool vacation;              /* the setting HoL */
    int16_t tmax;               /* the setting tbo, °C */
    bool pump_on;
    sh_pump_reason_t reason;
    uint8_t alarms;             /* SH_ALARM_BIT() of sh_alarm_t */
    bool lock;                  /* the keyboard is locked */
} sh_hap_in_t;

/* The characteristics that are notified when they change. */
typedef struct {
    uint8_t active;             /* 1 normal, 0 vacation */
    uint8_t cur_state;          /* sh_hap_cur_t */
    int16_t threshold;          /* °C */
    uint8_t lock;               /* 0 / 1 */
    uint8_t col_fault;          /* Status Fault of "Collector": 0 / 1 */
    uint8_t tank_fault;         /* Status Fault of "Tank": 0 / 1 */
} sh_hap_view_t;

void sh_hap_view(const sh_hap_in_t *in, sh_hap_view_t *out);

#define SH_HAP_CH_ACTIVE        0x01
#define SH_HAP_CH_CUR_STATE     0x02
#define SH_HAP_CH_THRESHOLD     0x04
#define SH_HAP_CH_LOCK          0x08
#define SH_HAP_CH_COL_FAULT     0x10
#define SH_HAP_CH_TANK_FAULT    0x20

/* The characteristics to notify: those that differ between `was` and `now` (0: nothing to send). */
unsigned sh_hap_changed(const sh_hap_view_t *was, const sh_hap_view_t *now);

/* A temperature for HomeKit (answered on a read only): `t` held in `min` ... `max`; false if there
 * is no value yet (SH_TEMP_INVALID) — the characteristic keeps what it has. A faulty sensor gives
 * its last valid value, the Status Fault tells the rest. */
bool sh_hap_temp(int16_t t, int16_t min, int16_t max, int16_t *out);

/* Writes of a controller: false = refused (invalid value), nothing changes. */
bool sh_hap_write_active(int v, bool *vacation);        /* 0 -> the vacation on, 1 -> off */
bool sh_hap_write_target(int v);                        /* Heat only */
bool sh_hap_write_threshold(float v, int16_t *tmax);    /* rounded to 1 °C, 30 ... 85 */
bool sh_hap_write_units(int v, bool *fahrenheit);       /* 0 °C, 1 °F */
bool sh_hap_write_lock(int v, bool *lock);              /* 0 / 1 */

/* The keyboard lock is set from a source (the buttons, Apple Home, the web interface — all the
 * same). True if it changed: then it is stored, "LoC" / "UnL" is shown and HomeKit is notified
 * (sh_hap_changed(): SH_HAP_CH_LOCK). */
bool sh_hap_lock_change(bool *lock, bool want);

/* ---- the display units: converted only where a value is shown or entered ------------------ */

/* A temperature as it is shown: tenths of °C, or of °F (to 0.1 °F) with the display units of
 * HomeKit (SH_TEMP_INVALID stays). */
int16_t sh_hap_display_temp(int16_t t, bool fahrenheit);

/* What a value in whole units is: a temperature, a temperature difference, or neither. */
typedef enum {
    SH_HAP_QTY_NONE = 0,        /* seconds, on / off ...: never converted */
    SH_HAP_QTY_TEMP,            /* °C  ->  °F = °C * 1.8 + 32 */
    SH_HAP_QTY_DIFF,            /* K   ->  °F = K * 1.8 (no + 32) */
} sh_hap_qty_t;

/* A whole value as it is shown: °C / K, or rounded to a whole °F. The stored value and its step
 * stay 1 °C / 1 K, so in °F the shown values step unevenly (tbo: 86, 88, 90, 91 ...). */
int16_t sh_hap_display_whole(int16_t v, sh_hap_qty_t qty, bool fahrenheit);

/* The value to store for an entered one (the web interface): the nearest whole °C / K. What
 * sh_hap_display_whole() shows comes back as the same stored value. */
int16_t sh_hap_entered_whole(int16_t shown, sh_hap_qty_t qty, bool fahrenheit);

/* The quantity of a menu setting: tbo and C.Lo are temperatures, d.on and d.oF differences. */
sh_hap_qty_t sh_hap_setting_qty(sh_setting_t id);

/* ---- when HAP runs ---------------------------------------------------------------------- */

/* HAP has to be started now: the setting is on, the device is connected to the router (not the
 * setup portal, not the own access point) and HAP has not been started since boot. */
bool sh_hap_start_due(bool enabled, bool connected, bool started);

/* The own web server (and the own mDNS) is the one to use: always with the setting off; with it on
 * only on the own access point before HAP was started — once HAP is started (`started`: or being
 * started) its server holds port 80 and serves the pages on every interface. `connected`: to the
 * router; `ap`: the own access point is on. */
bool sh_hap_own_web(bool enabled, bool started, bool connected, bool ap);

/* A change of the setting "Apple HomeKit" (the menu position HAP, the switch of the web interface). */
typedef enum {
    SH_HAP_SET_SAME = 0,        /* nothing to do */
    SH_HAP_SET_REFUSED,         /* off while paired: first remove the device from Apple Home */
    SH_HAP_SET_RESTART,         /* store it and restart */
} sh_hap_set_t;
sh_hap_set_t sh_hap_set(bool enabled, bool want, bool paired);

/* The pairing window (the LED breathes while it is open) follows the events of esp-homekit-sdk:
 * it opens when HAP starts without a paired controller and when the last controller is removed,
 * and closes on a pairing and when its 10 min are over (HAP keeps running). */
typedef enum {
    SH_HAP_EVT_STARTED,         /* hap_start() done */
    SH_HAP_EVT_PAIRED,          /* HAP_EVENT_CTRL_PAIRED */
    SH_HAP_EVT_UNPAIRED,        /* HAP_EVENT_CTRL_UNPAIRED */
    SH_HAP_EVT_TIMED_OUT,       /* HAP_EVENT_PAIRING_MODE_TIMED_OUT */
} sh_hap_evt_t;

/* The window after `evt`; `paired`: controllers paired now (hap_get_paired_controller_count()). */
bool sh_hap_window(bool open, sh_hap_evt_t evt, int paired);
