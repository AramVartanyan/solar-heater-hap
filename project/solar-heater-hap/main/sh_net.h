/* The network (group E; doc/tasks/step7-network-ota.md): Wi-Fi through captive-wifi 0.4.0, the mode
 * CP / AP of the menu position nEt, the name in the network and the firmware update. One task,
 * "sh_net", takes the events of captive-wifi and of general-ota, the choices of the menu and of
 * the web interface, and does what the rules say (sh_net_rules, sh_ota — plain C, host test):
 *
 *   CP   captive_wifi_start(): the router, the clock (SNTP), the web interface, mDNS
 *        ("solar-xxxxxx.local", _http._tcp); without a stored network the setup portal, which
 *        captive-wifi closes after 15 min without a client (CONFIG_CAPTIVE_WIFI_PORTAL_IDLE_MIN):
 *        the radio stays off until a restart or a choice of nEt
 *   AP   captive_wifi_start_ap(15 min): the open access point "SOLAR-xxxxxx" without a sign-in
 *        window, the web interface at http://192.168.4.1; after 15 min without a client back to
 *        CP (with a stored network to the router, without one the radio goes off)
 * The mode is not stored: CP at every boot. The name in the network (DHCP hostname, mDNS) is the
 * access point name in lower case ("solar-810841").
 * Firmware update (general-ota, CONFIG_GENERAL_OTA_URL): the automatic check 1 min after boot, "FUp"
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

/* nEt chosen in the menu or in the web interface (any task). */
void sh_net_set_mode(sh_net_mode_t mode);

/* A firmware update command of the web interface or of the "menu" button (any task). */
void sh_net_update(sh_ota_cmd_t cmd);

/* The access code, as on the display after nEt -> AP: "810841" (after sh_net_init()). */
const char *sh_net_access_code(void);

/* The name in the network: "solar-810841" (after sh_net_init()). */
const char *sh_net_name(void);

/* What the web interface shows. */
typedef struct {
    sh_net_mode_t mode;
    bool ap_pending;            /* AP chosen during an update: it starts after it */
    sh_ota_phase_t ota;
    int8_t ota_pct;
    char ota_version[SH_OTA_VERSION_MAX];
} sh_net_info_t;

void sh_net_get(sh_net_info_t *out);
