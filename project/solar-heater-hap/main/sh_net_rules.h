/* The network mode — position nEt of the menu (doc/operating-modes.md 2.5, "Решения за група E";
 * doc/tasks/step7-network-ota.md 3.1, 3.2) — plain C, no ESP-IDF / FreeRTOS, compiled into the
 * host test too. sh_net feeds the choices and the events of captive-wifi and does what the rules
 * ask for.
 *
 *   CP  the router (or, without a stored network, the setup portal, which captive-wifi closes after
 *       15 min without a client: the radio is then off until a restart or a choice of nEt)
 *   AP  the own open access point "SOLAR-xxxxxx" with the web interface at 192.168.4.1; after
 *       15 min without a client — counted from the start and from the last client leaving — back
 *       to CP: with a stored network to the router, without one the radio goes off
 * The mode is not stored: CP at every boot. A choice of AP during a firmware update waits for its
 * end. Times in seconds of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SH_NET_AP_IDLE_S        (15u * 60u)     /* AP: back to CP after this long without a client */
#define SH_NET_CODE_LEN         6               /* the access code: the end of "SOLAR-xxxxxx" */
#define SH_NET_NAME_MAX         33              /* a network name with its NUL */

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

/* A choice of the menu or of the web interface. `updating`: a firmware update is running. Choosing
 * CP in CP starts the radio again if it is off, otherwise it changes nothing. */
sh_net_act_t sh_net_choose(sh_net_t *n, sh_net_mode_t mode, bool updating, uint32_t now_s);

/* An event of captive-wifi. `has_network`: a network is stored. */
sh_net_act_t sh_net_event(sh_net_t *n, sh_net_evt_t evt, bool has_network, uint32_t now_s);

/* Once a second (or more often): the postponed AP, the idle time of the AP. */
sh_net_act_t sh_net_step(sh_net_t *n, bool updating, bool has_network, uint32_t now_s);

/* The access code: the last 6 characters of the access point name ("SOLAR-810841" -> "810841"). */
const char *sh_net_code(const char *ap_ssid);

/* The name in the network (DHCP, mDNS): the access point name in lower case
 * ("SOLAR-810841" -> "solar-810841"). */
void sh_net_hostname(char *out, size_t size, const char *ap_ssid);
