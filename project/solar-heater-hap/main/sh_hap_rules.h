/* Apple HomeKit: what the accessory shows and accepts, and when HAP runs (doc/operating-modes.md 3,
 * "Включване на HAP", "Известия към HomeKit"; doc/tasks/step14-homekit.md 3.1 - 3.3) — plain C, no
 * ESP-IDF / esp-homekit-sdk, compiled into the host test too. sh_hap creates the accessory and feeds
 * these rules.
 *
 *   Heater Cooler (primary)  Active        on = normal mode, off = vacation (and back: the menu and
 *                                          the web interface -> Active)
 *                            Current State Idle = the pump stands, Heating = it runs (charging, a
 *                                          protection ...), Cooling = the night cooling
 *                            Target State  only Heat
 *                            Current Temp  the tank, 0 ... 100 °C, step 0.1
 *                            Heating Thr.  Tmax, 30 ... 85 °C, step 1
 *                            Display Units the units of our display (the values are always °C)
 *                            Lock Physical the keypad lock (the same variable as on the buttons)
 *   Temperature "Collector"  -40 ... 200 °C, Status Fault: E1, E5
 *   Temperature "Tank"       0 ... 100 °C, Status Fault: E2, E4
 * A faulty sensor keeps the last value; in the pulse mode the last reliable values are shown. The
 * temperatures are never notified (the controllers read them); a change of Active, Current State,
 * Heating Threshold, Lock Physical Controls, Temperature Display Units or a Status Fault is.
 *
 * HAP runs only with the setting "Apple HomeKit" on and only once the device is connected to the
 * router (not with the setup portal, not in AP); once started it is never stopped. With HAP on the
 * web pages are on the server of HAP; the own server serves them only on the own access point
 * before HAP has started (the station is off in AP, so HAP can start only after the AP is over, and
 * the own server is stopped first). Switching HAP off while paired is refused. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* HAP values (HomeKit Accessory Protocol) */
#define SH_HAP_STATE_IDLE       1       /* Current Heater Cooler State */
#define SH_HAP_STATE_HEATING    2
#define SH_HAP_STATE_COOLING    3
#define SH_HAP_TARGET_HEAT      1       /* Target Heater Cooler State: the only valid value */

#define SH_HAP_TANK_MIN         0       /* °C: Current Temperature of the heater and of "Tank" */
#define SH_HAP_TANK_MAX         100
#define SH_HAP_COL_MIN          (-40)   /* °C: "Collector" */
#define SH_HAP_COL_MAX          200
#define SH_HAP_TMAX_MIN         30      /* Heating Threshold = Tmax (the menu range of tbo) */
#define SH_HAP_TMAX_MAX         85

/* What the controller has, as the accessory shows it. */
typedef struct {
    bool vacation;
    bool pump_on;
    bool cooling;               /* the pump runs by the night cooling */
    int16_t t_tank, t_col;      /* shown values, 0.1 °C, SH_TEMP_INVALID: none */
    bool tank_fault, col_fault;
    uint8_t alarms;             /* SH_ALARM_BIT() of sh_alarm_t */
    int16_t tmax;               /* tbo, °C */
    bool locked;                /* the keypad lock */
    bool fahrenheit;            /* the display units */
} sh_hap_in_t;

/* The characteristics. Temperatures in 0.1 °C. */
typedef struct {
    uint8_t active;             /* 1 normal, 0 vacation */
    uint8_t state;              /* SH_HAP_STATE_* */
    int16_t tank, col;
    uint8_t threshold;          /* °C */
    uint8_t units;              /* 0 °C, 1 °F */
    uint8_t lock;               /* 0 / 1 */
    uint8_t col_fault, tank_fault;  /* Status Fault 0 / 1 */
} sh_hap_view_t;

/* `in` -> the characteristics; `prev` gives the temperatures kept while a sensor is faulty or has
 * no value yet (the first view: prev = NULL -> 0 °C, as created). */
void sh_hap_view(const sh_hap_in_t *in, const sh_hap_view_t *prev, sh_hap_view_t *out);

/* Which characteristics to notify: a bit for each that changed (the temperatures never). */
#define SH_HAP_N_ACTIVE         0x01u
#define SH_HAP_N_STATE          0x02u
#define SH_HAP_N_THRESHOLD      0x04u
#define SH_HAP_N_UNITS          0x08u
#define SH_HAP_N_LOCK           0x10u
#define SH_HAP_N_COL_FAULT      0x20u
#define SH_HAP_N_TANK_FAULT     0x40u
unsigned sh_hap_notify(const sh_hap_view_t *prev, const sh_hap_view_t *now);

/* Writes of a controller: true = valid (the value to apply in `*out`), false = HAP_STATUS_VAL_INVALID. */
bool sh_hap_write_active(uint8_t v, bool *vacation);            /* 0 / 1 -> vacation on / off */
bool sh_hap_write_target(uint8_t v);                            /* only Heat */
bool sh_hap_write_threshold(float c, int16_t *tmax);            /* rounded to a whole °C, 30 ... 85 */
bool sh_hap_write_flag(uint8_t v, bool *on);                    /* units (1 = °F), lock: 0 / 1 */

/* HAP starts now: the setting is on, not started yet, connected to the router. */
bool sh_hap_should_start(bool enabled, bool started, bool connected);

/* The own web server runs: without HAP while connected to the router or on the own access point;
 * with HAP only on the own access point before HAP has started (afterwards HAP's server serves). */
bool sh_hap_own_web(bool enabled, bool started, bool connected, bool ap);

/* A change of the setting "Apple HomeKit" (the menu, the web interface). */
typedef enum {
    SH_HAP_SW_NONE = 0,         /* the same value: nothing */
    SH_HAP_SW_RESTART,          /* stored, then the controller restarts */
    SH_HAP_SW_REFUSED,          /* off while paired: first remove the device from Apple Home */
} sh_hap_sw_t;
sh_hap_sw_t sh_hap_switch(bool on, bool want, bool paired);

/* The pairing window of the SDK (doc/firmware.md 3): open at the start without paired controllers,
 * when the last controller is removed; closed by a pairing or the timeout (10 min). */
typedef enum {
    SH_HAP_W_STARTED,           /* hap_start() done */
    SH_HAP_W_PAIRED,            /* HAP_EVENT_CTRL_PAIRED */
    SH_HAP_W_UNPAIRED,          /* HAP_EVENT_CTRL_UNPAIRED */
    SH_HAP_W_TIMEOUT,           /* HAP_EVENT_PAIRING_MODE_TIMED_OUT */
} sh_hap_w_evt_t;
bool sh_hap_window(bool open, sh_hap_w_evt_t evt, int paired_count);
