/* The network (doc/operating-modes.md 6.2, doc/web-interface.md 5): Wi-Fi through captive-wifi
 * 0.4.0, the mode CP / AP of the menu position nEt, the name in the network and the firmware update. One task,
 * "sh_net", takes the events of captive-wifi and of general-ota, the choices of the menu and of
 * the web interface, and does what the rules say (sh_net_rules, sh_ota — plain C, host test):
 *
 *   CP   captive_wifi_start(): the router, the clock (SNTP), the web interface, mDNS
 *        ("Solar-xxxxxx.local", _http._tcp); without a stored network the setup portal, which
 *        captive-wifi closes after 15 min without a client (CONFIG_CAPTIVE_WIFI_PORTAL_IDLE_MIN):
 *        the radio stays off until a restart or a choice of nEt
 *   AP   captive_wifi_start_ap(15 min): the open access point "Solar-xxxxxx" without a sign-in
 *        window, the web interface at http://192.168.4.1; after 15 min without a client back to
 *        CP (with a stored network to the router, without one the radio goes off)
 * AP is chosen in the menu only and only while the device is not connected to the router. The mode
 * is not stored: CP at every boot. One name everywhere — captive_wifi_ap_ssid(), built from the base
 * MAC ("Solar-810840"): the network of the portal / AP, the DHCP hostname and mDNS; its last 6
 * characters are the access code, the base MAC in capitals is the serial number.
 * Apple HomeKit (sh_hap): with the setting on, HAP is started on the first connection to the
 * router; from then on its HTTP server (port 80) serves the web interface and its mDNS carries the
 * name — mDNS is then started by HAP and only afterwards given the name and the web service. With
 * the setting off (and on the own access point before HAP was started) the own server and, on the
 * router, the own mDNS are used.
 * Firmware update (general-ota, CONFIG_GENERAL_OTA_URL): one automatic check after boot, "FUp"
 * and its confirmation, the check and the update from the web interface, the wait for the pump —
 * sh_ota; the restart after a written update is the one of general-ota. */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sh_net_rules.h"
#include "sh_ota.h"

/* captive-wifi (esp_netif, Wi-Fi driver), the name in the network, mDNS, general-ota. */
esp_err_t sh_net_init(void);

/* The network task; starts Wi-Fi in CP. After sh_web_init(). */
esp_err_t sh_net_start(void);

/* nEt chosen in the menu (any task). AP is refused while connected to the router. */
void sh_net_set_mode(sh_net_mode_t mode);

/* A firmware update command of the web interface or of the "menu" button (any task). */
void sh_net_update(sh_ota_cmd_t cmd);

/* The access code, as on the display after nEt -> AP: "810840" (after sh_net_init()). */
const char *sh_net_access_code(void);

/* The name in the network: "Solar-810840" (after sh_net_init()). */
const char *sh_net_name(void);

/* The serial number: "E8F60A810840" (after sh_net_init()). */
const char *sh_net_serial_number(void);

/* mDNS: the name "Solar-xxxxxx.local" and the web service (_http._tcp, port 80). `own`: mDNS is
 * started here (HAP is not used); otherwise HAP has started it (hap_start()) and only the name and
 * the service are set. */
esp_err_t sh_net_mdns_start(bool own);

/* HAP could not be started (sh_hap): the own web server and the own mDNS take over. Any task. */
void sh_net_hap_failed(void);

/* What the web interface shows. */
typedef struct {
    sh_net_mode_t mode;
    bool ap_pending;            /* AP chosen during an update: it starts after it */
    sh_ota_phase_t ota;
    int8_t ota_pct;
    char ota_version[SH_OTA_VERSION_MAX];
} sh_net_info_t;

void sh_net_get(sh_net_info_t *out);
