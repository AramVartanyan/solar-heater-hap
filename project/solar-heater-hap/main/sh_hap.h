/* Apple HomeKit (doc/operating-modes.md 3): the accessory on
 * esp-homekit-sdk. What it shows and takes is decided by sh_hap_rules (plain C, host test); this
 * module builds the accessory, starts HAP and passes the values on.
 *
 *   accessory   "Solar-xxxxxx" (the name in the network), category Heater, Armo Ltd., Solar Heater,
 *               serial number = the base MAC, the firmware version
 *   services    Heater Cooler (primary), Temperature Sensor "Collector", Temperature Sensor "Tank"
 *   setup code  from factory_nvs (the product); CONFIG_SH_HAP_FIXED_SETUP_CODE — a fixed code for
 *               development only
 *   setting     "Apple HomeKit", on by default, NVS namespace "sh_hap" (the factory reset brings the
 *               default back); a change is stored and the controller restarts; off is refused while
 *               a controller is paired
 * HAP is started once, by sh_net, on the first connection to the router with the setting on —
 * hap_init() and hap_start() in the task of this module — and never stopped. Then the pages of the
 * web interface are registered on the HTTP server of HAP and "Solar-xxxxxx.local" is set in the mDNS
 * that HAP has started. With the setting off (or if HAP could not start) the own server and the own
 * mDNS are used, as before.
 * A task, "sh_hap": the start, the events of the SDK (pairing window, paired / unpaired) and, once a
 * second, the notifications of what has changed. The temperatures are answered on a read only. */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* The stored setting, the display units and the keyboard lock; the task. After nvs_flash_init() and
 * sh_state_init(), before sh_net_init(). */
esp_err_t sh_hap_init(void);

/* The setting "Apple HomeKit" as stored (after a change: the new value, the restart follows). */
bool sh_hap_enabled(void);

/* HAP is to be used since this boot: the setting was on at boot and HAP has not failed to start. */
bool sh_hap_wanted(void);

/* HAP is started or being started: port 80 is its. */
bool sh_hap_started(void);

/* HAP is running: the pairings can be reset. */
bool sh_hap_running(void);

/* A controller is paired (before HAP is started: as it was when HAP last ran). */
bool sh_hap_paired(void);

/* Starts HAP (in the task of this module); sh_net calls it when sh_hap_start_due() says so. */
void sh_hap_start(void);

/* Changes the setting: stores it and restarts the controller 1 s later. ESP_ERR_INVALID_STATE: off
 * is refused while a controller is paired. Nothing happens if it is the same. */
esp_err_t sh_hap_set_enabled(bool on);

/* The keyboard lock — the Lock Physical Controls of HomeKit, one variable for the buttons ("+" and
 * "-" held 3 s, the menu position LoC), Apple Home and the web interface; kept in NVS, also with
 * HAP off. A change is stored, shown on the display ("LoC" / "UnL") and notified to HomeKit.
 * `source`: for the log. Any task. */
void sh_hap_set_lock(bool on, const char *source);

/* "Reset -> Default settings" of the web interface (no restart): the keyboard unlocked, the units
 * °C — as an ordinary change of each (stored, shown, notified). The setting "Apple HomeKit" and the
 * pairing stay. The vacation and Tmax are reset by sh_settings and notified like any change. */
void sh_hap_defaults_reset(void);

/* Erases the pairings (the controller restarts). ESP_ERR_INVALID_STATE: HAP is not running. */
esp_err_t sh_hap_reset_pairings(void);
