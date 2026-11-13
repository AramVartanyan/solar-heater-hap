/* Created by Aram Vartanyan (c) 2026 */

/* The network mode — position nEt of the menu (doc/operating-modes.md 6.2)
 * — plain C, no ESP-IDF / FreeRTOS, compiled into the
 * host test too. sh_net feeds the choices and the events of captive-wifi and does what the rules
 * ask for.
 *
 *   CP  the router (or, without a stored network, the setup portal, which captive-wifi closes after
 *       15 min without a client: the radio is then off until a restart or a choice of nEt)
 *   AP  the own open access point "Solar-xxxxxx" with the web interface at 192.168.4.1; after
 *       15 min without a client — counted from the start and from the last client leaving — back
 *       to CP: with a stored network to the router, without one the radio goes off
 * AP can be chosen only while the device is not connected to the router (no stored network, the
 * router does not answer, or the radio is off); with a connection the choice is refused. The mode
 * is chosen in the menu only and is not stored: CP at every boot. A choice of AP during a firmware
 * update waits for its end.
 * One name, taken from the base MAC address (e8:f6:0a:81:08:40): "Solar-810840" — the network of
 * the portal / AP, the host name at the router and mDNS (captive-wifi builds it); its last 6
 * characters are the access code; the serial number is the whole MAC in capitals.
 * Times in seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SH_NET_AP_IDLE_S        (15u * 60u)     /* AP: back to CP after this long without a client */
#define SH_NET_CODE_LEN         6               /* the access code: the end of "Solar-xxxxxx" */
#define SH_NET_SERIAL_LEN       12              /* the serial number: the base MAC, hex */

typedef enum {
    SH_NET_CP = 0,              /* router / setup portal */
    SH_NET_AP,                  /* own access point */
} sh_net_mode_t;

/* What captive-wifi reports. */
typedef enum {
    SH_NET_EVT_AP_STARTED,      /* the access point is up */
    SH_NET_EVT_AP_TIMEOUT,      /* captive-wifi closed it after its idle time (radio off) */
    SH_NET_EVT_CLIENT_JOINED,
    SH_NET_EVT_CLIENT_LEFT,
    SH_NET_EVT_PORTAL_TIMEOUT,  /* no stored network, no client on the portal: radio off */
} sh_net_evt_t;

/* What sh_net has to do. */
typedef enum {
    SH_NET_ACT_NONE = 0,
    SH_NET_ACT_START_AP,        /* captive_wifi_start_ap(SH_NET_AP_IDLE_S) */
    SH_NET_ACT_START_CP,        /* captive_wifi_start(): the router or the portal */
    SH_NET_ACT_RADIO_OFF,       /* captive_wifi_stop() */
} sh_net_act_t;

/* All zeros at boot: CP. */
typedef struct {
    sh_net_mode_t mode;
    bool ap_pending;            /* AP was chosen during an update: it starts after it */
    bool radio_off;             /* CP: the radio is off (the portal or the AP ran out of time) */
    uint8_t clients;            /* AP: phones on it */
    uint32_t idle_s;            /* AP: the start of the time without a client */
} sh_net_t;

/* A choice of the menu. `connected`: the device is connected to the router — AP is refused then
 * (nothing changes). `updating`: a firmware update is running. Choosing CP in CP starts the radio
 * again if it is off, otherwise it changes nothing. */
sh_net_act_t sh_net_choose(sh_net_t *n, sh_net_mode_t mode, bool connected, bool updating, uint32_t now_s);

/* An event of captive-wifi. `has_network`: a network is stored. */
sh_net_act_t sh_net_event(sh_net_t *n, sh_net_evt_t evt, bool has_network, uint32_t now_s);

/* Once a second (or more often): the postponed AP, the idle time of the AP. */
sh_net_act_t sh_net_step(sh_net_t *n, bool updating, bool has_network, uint32_t now_s);

/* The access code: the last 6 characters of the name ("Solar-810840" -> "810840"). */
const char *sh_net_code(const char *name);

/* The serial number: the base MAC in capitals (e8:f6:0a:81:08:40 -> "E8F60A810840"). */
void sh_net_serial(char out[SH_NET_SERIAL_LEN + 1], const uint8_t mac[6]);

/* The name ends with "-" and the last 3 bytes of the base MAC in lower case, as the code and the
 * serial number expect it ("Solar-810840" for ...:81:08:40). */
bool sh_net_name_ok(const char *name, const uint8_t mac[6]);
