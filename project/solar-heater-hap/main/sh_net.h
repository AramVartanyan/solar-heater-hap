/* Network mode: CP (the router / the setup portal) or AP (the own access point for the web interface)
 * (doc/operating-modes.md 2.5 "nEt", "Decisions for group E"; doc/tasks/step7-network-ota.md 3.1, 3.2)
 * — plain C, no ESP-IDF / FreeRTOS, compiled into the host test too. sh_link applies the actions with
 * captive-wifi.
 *
 *   start            always CP (the mode is not stored)
 *   select AP        from the menu (nEt), only while the controller is not connected to the router (no
 *                    network stored, the router does not answer, or the radio is off); connected:
 *                    refused. The access point for 15 min; deferred while a firmware update is busy
 *                    (checking, waiting for the pump, writing) and started when it is over. The web
 *                    interface only shows the mode
 *   AP               15 min without a client, or 15 min after the last client left -> CP: with a
 *                    stored network back to the router, without one the radio stays off
 *   select CP        from AP: back to the router / the portal; in CP after the portal closed
 *                    (15 min without a client, no network stored): the portal again
 * The idle time of the access point is counted here (to the second) and by captive-wifi
 * (captive_wifi_start_ap(15 min)); whichever comes first ends it. Times: monotonic seconds. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SH_NET_AP_IDLE_S        (15u * 60u)     /* the access point without a client */

typedef enum {
    SH_NET_CP = 0,
    SH_NET_AP,
} sh_net_mode_t;

typedef enum {
    SH_NET_ACT_NONE = 0,
    SH_NET_ACT_START_AP,        /* captive_wifi_start_ap(SH_NET_AP_IDLE_S) */
    SH_NET_ACT_START_CP,        /* captive_wifi_start(): the router, or the portal without a network */
    SH_NET_ACT_RADIO_OFF,       /* captive_wifi_stop(): the access point ends, no network stored */
} sh_net_act_t;

typedef struct {
    sh_net_mode_t mode;         /* the mode in use (the menu and the web interface show it) */
    bool ap_pending;            /* AP chosen during a firmware update: starts after it */
    bool radio_off;             /* CP: the portal closed for want of a client (until a choice / restart) */
    uint8_t clients;            /* phones on the access point */
    uint32_t idle_since_s;      /* AP: the start, or the last client leaving */
} sh_net_t;

/* At boot: CP. */
void sh_net_init(sh_net_t *n);

/* AP may be chosen: not connected to the router. */
bool sh_net_ap_allowed(bool connected);

/* The menu chose `mode`; `connected`: to the router (AP is refused then); `fw_busy`: a firmware update
 * is busy. */
sh_net_act_t sh_net_select(sh_net_t *n, sh_net_mode_t mode, bool connected, bool fw_busy, uint32_t now_s);

/* The firmware update is over (written, failed or given up): a deferred AP starts now. */
sh_net_act_t sh_net_fw_done(sh_net_t *n, uint32_t now_s);

/* A phone joined / left the access point. */
void sh_net_client(sh_net_t *n, bool joined, uint32_t now_s);

/* Once a second: the idle time of the access point. `stored`: a network is stored. */
sh_net_act_t sh_net_tick(sh_net_t *n, uint32_t now_s, bool stored);

/* captive-wifi ended the access point by its idle time (AP_TIMEOUT). */
sh_net_act_t sh_net_ap_timeout(sh_net_t *n, bool stored);

/* captive-wifi closed the portal for want of a client (PORTAL_TIMEOUT): the radio is off. */
void sh_net_portal_timeout(sh_net_t *n);

/* Seconds left before the idle access point ends (0 with a client or outside AP). */
uint32_t sh_net_ap_left(const sh_net_t *n, uint32_t now_s);

/* The name of the controller, "<prefix>-xxxxxx" (e.g. "Solar-810840"): the last 3 bytes of the base MAC
 * in lower case — the same text as captive_wifi_ap_ssid(): the network of the portal / AP, the host
 * name for the router (DHCP) and mDNS ("Solar-810840.local"). `size` >= 32. */
#define SH_NET_NAME_MAX         32
void sh_net_name(char *buf, size_t size, const char *prefix, const uint8_t mac[6]);

/* The access code of "Service" / "System": the last 6 characters of the name ("810840"). */
#define SH_NET_CODE_LEN         6
void sh_net_code(char *buf, size_t size, const char *name);

/* The serial number: the base MAC, 12 hex digits in upper case ("E8F60A810840"). `size` >= 13. */
void sh_net_serial(char *buf, size_t size, const uint8_t mac[6]);
