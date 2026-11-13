/* Network mode — see sh_net.h. */

#include <stdio.h>
#include <string.h>
#include "sh_net.h"

void sh_net_init(sh_net_t *n)
{
    *n = (sh_net_t){ .mode = SH_NET_CP };
}

static sh_net_act_t start_ap(sh_net_t *n, uint32_t now_s)
{
    n->mode = SH_NET_AP;
    n->ap_pending = false;
    n->radio_off = false;
    n->clients = 0;
    n->idle_since_s = now_s;
    return SH_NET_ACT_START_AP;
}

/* The end of the access point: the router with a stored network, otherwise the radio stays off. */
static sh_net_act_t end_ap(sh_net_t *n, bool stored)
{
    n->mode = SH_NET_CP;
    n->clients = 0;
    n->radio_off = !stored;
    return stored ? SH_NET_ACT_START_CP : SH_NET_ACT_RADIO_OFF;
}

bool sh_net_ap_allowed(bool connected)
{
    return !connected;
}

sh_net_act_t sh_net_select(sh_net_t *n, sh_net_mode_t mode, bool connected, bool fw_busy, uint32_t now_s)
{
    if (mode == SH_NET_AP) {
        if (n->mode == SH_NET_AP || !sh_net_ap_allowed(connected)) {
            return SH_NET_ACT_NONE;         /* connected to the router: refused */
        }
        if (fw_busy) {
            n->ap_pending = true;           /* after the update */
            return SH_NET_ACT_NONE;
        }
        return start_ap(n, now_s);
    }
    n->ap_pending = false;                  /* CP cancels a deferred AP */
    if (n->mode == SH_NET_AP || n->radio_off) {
        n->mode = SH_NET_CP;
        n->clients = 0;
        n->radio_off = false;
        return SH_NET_ACT_START_CP;         /* the router, or the portal without a network */
    }
    return SH_NET_ACT_NONE;
}

sh_net_act_t sh_net_fw_done(sh_net_t *n, uint32_t now_s)
{
    return n->ap_pending && n->mode == SH_NET_CP ? start_ap(n, now_s) : SH_NET_ACT_NONE;
}

void sh_net_client(sh_net_t *n, bool joined, uint32_t now_s)
{
    if (n->mode != SH_NET_AP) {
        return;
    }
    if (joined) {
        n->clients++;
    } else if (n->clients > 0) {
        n->clients--;
        if (n->clients == 0) {
            n->idle_since_s = now_s;        /* 15 min after the last one left */
        }
    }
}

sh_net_act_t sh_net_tick(sh_net_t *n, uint32_t now_s, bool stored)
{
    if (n->mode == SH_NET_AP && n->clients == 0 && now_s - n->idle_since_s >= SH_NET_AP_IDLE_S) {
        return end_ap(n, stored);
    }
    return SH_NET_ACT_NONE;
}

sh_net_act_t sh_net_ap_timeout(sh_net_t *n, bool stored)
{
    return n->mode == SH_NET_AP ? end_ap(n, stored) : SH_NET_ACT_NONE;
}

void sh_net_portal_timeout(sh_net_t *n)
{
    if (n->mode == SH_NET_CP) {
        n->radio_off = true;
    }
}

uint32_t sh_net_ap_left(const sh_net_t *n, uint32_t now_s)
{
    if (n->mode != SH_NET_AP || n->clients > 0) {
        return 0;
    }
    const uint32_t idle = now_s - n->idle_since_s;
    return idle < SH_NET_AP_IDLE_S ? SH_NET_AP_IDLE_S - idle : 0;
}

void sh_net_name(char *buf, size_t size, const char *prefix, const uint8_t mac[6])
{
    /* as captive-wifi: "<prefix>-xxxxxx", the prefix cut to 24 characters */
    snprintf(buf, size, "%.24s-%02x%02x%02x", prefix, mac[3], mac[4], mac[5]);
}

void sh_net_code(char *buf, size_t size, const char *name)
{
    const size_t len = strlen(name);
    snprintf(buf, size, "%s", len >= SH_NET_CODE_LEN ? name + len - SH_NET_CODE_LEN : name);
}

void sh_net_serial(char *buf, size_t size, const uint8_t mac[6])
{
    snprintf(buf, size, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
