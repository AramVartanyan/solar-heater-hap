/* The network and the firmware update (doc/tasks/step7-network-ota.md): one task that runs the
 * network mode (sh_net: CP / AP with captive-wifi), the name in the network (DHCP hostname and mDNS
 * "solar-xxxxxx.local" with the web service, in CP while connected to the router), the clock and the
 * web interface on the connection, and the firmware update (sh_fw with general-ota: the automatic
 * check 1 min after the restart, "FUp", the start when the pump is off).
 * The calls below only queue a request: the work is done by the task. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sh_net.h"
#include "sh_fw.h"

/* Wi-Fi (captive-wifi), mDNS, general-ota and the task; after the measurement and the control start. */
esp_err_t sh_link_start(void);

/* "solar-xxxxxx" */
const char *sh_link_hostname(void);

/* The menu (nEt) or the web interface chose a network mode. */
void sh_link_select(sh_net_mode_t mode);

/* The web interface: check for an update / install the newer version; the menu: confirm "FUp". */
void sh_link_fw_check(void);
void sh_link_fw_update(void);
void sh_link_fw_confirm(void);

/* What the web interface shows. */
typedef struct {
    sh_net_mode_t mode;
    bool ap_pending;            /* AP waits for the end of the firmware update */
    uint8_t clients;            /* phones on the access point */
    uint32_t ap_left_s;         /* the idle access point ends in */
    sh_fw_state_t fw;
    uint8_t fw_pct;
    char fw_ver[SH_FW_VER_LEN];
} sh_link_info_t;
void sh_link_get(sh_link_info_t *out);
