/* Apple HomeKit (group F; doc/operating-modes.md 3; doc/tasks/step14-homekit.md): the accessory of
 * esp-homekit-sdk — Heater Cooler (primary) and the temperature sensors "Collector" and "Tank" — fed
 * by the rules of sh_hap_rules (plain C, host test).
 *
 * The setting "Apple HomeKit" (NVS "sh_hap", key "on"; on by default, the factory reset brings it
 * back; "Default settings" does not touch it) is read at boot; a change is stored and the controller
 * restarts. With it on, the accessory is created at boot (hap_init(): the pairings are known) and
 * HAP starts on the first connection to the router (sh_net) and is never stopped (hap_stop() is not
 * used). After hap_start() — which starts mDNS (hap_mdns_init()) — the name Solar-xxxxxx and the web
 * service (_http._tcp, port 80) are set in its mDNS, and the pages of the web interface are
 * registered on its HTTP server (sh_web_attach()). The setup code: the setup info in factory_nvs;
 * for the development only a fixed code from Kconfig (CONFIG_SH_HAP_FIXED_SETUP_CODE). */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* After sh_net_init() (the name) and the settings: the setting; with it on hap_init() and the
 * accessory. */
esp_err_t sh_hap_init(void);

/* The setting "Apple HomeKit" (as read at boot: a change applies after the restart). */
bool sh_hap_enabled(void);

/* Stores the setting (the caller restarts the controller). */
esp_err_t sh_hap_store_enabled(bool on);

/* HAP has started. */
bool sh_hap_started(void);

/* Paired with at least one controller (false with the setting off). */
bool sh_hap_paired(void);

/* Connected to the router: starts HAP once (its own task) if the setting is on. The own web server
 * must be stopped before (port 80). */
void sh_hap_connected(void);

/* "Reset -> HomeKit pairings": erases the pairings, the SDK restarts the controller. Only while HAP
 * runs (ESP_ERR_INVALID_STATE otherwise). */
esp_err_t sh_hap_reset_pairings(void);

/* Factory reset while HAP runs: hap_reset_to_factory() (closes the sessions, ends the mDNS
 * announcement, erases the "nvs" partition, restarts). False: HAP does not run, erase it yourself. */
bool sh_hap_reset_to_factory(void);
