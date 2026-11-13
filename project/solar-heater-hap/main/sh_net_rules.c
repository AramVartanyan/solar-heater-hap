/* The network mode — see sh_net_rules.h. */

#include <string.h>
#include "sh_net_rules.h"

static sh_net_act_t start_ap(sh_net_t *n, uint32_t now_s)
{
    n->mode = SH_NET_AP;
    n->ap_pending = false;
    n->radio_off = false;
    n->clients = 0;
    n->idle_s = now_s;
    return SH_NET_ACT_START_AP;
}

sh_net_act_t sh_net_choose(sh_net_t *n, sh_net_mode_t mode, bool updating, uint32_t now_s)
{
    if (mode == SH_NET_AP) {
        if (n->mode == SH_NET_AP) {
            return SH_NET_ACT_NONE;
        }
        if (updating) {
            n->ap_pending = true;       /* the update needs the router: after its end */
            return SH_NET_ACT_NONE;
        }
        return start_ap(n, now_s);
    }
    n->ap_pending = false;
    if (n->mode == SH_NET_AP || n->radio_off) {
        n->mode = SH_NET_CP;
        n->radio_off = false;
        return SH_NET_ACT_START_CP;
    }
    return SH_NET_ACT_NONE;
}

/* The AP is over: with a stored network to the router, without one the radio goes off. */
static sh_net_act_t ap_over(sh_net_t *n, bool has_network, bool radio_is_off)
{
    n->mode = SH_NET_CP;
    n->clients = 0;
    if (has_network) {
        return SH_NET_ACT_START_CP;
    }
    n->radio_off = true;
    return radio_is_off ? SH_NET_ACT_NONE : SH_NET_ACT_RADIO_OFF;
}

sh_net_act_t sh_net_event(sh_net_t *n, sh_net_evt_t evt, bool has_network, uint32_t now_s)
{
    switch (evt) {
    case SH_NET_EVT_AP_STARTED:
        if (n->mode == SH_NET_AP) {
            n->clients = 0;
            n->idle_s = now_s;          /* the time runs from the real start */
        }
        break;
    case SH_NET_EVT_CLIENT_JOINED:
        if (n->mode == SH_NET_AP && n->clients < UINT8_MAX) {
            n->clients++;
        }
        break;
    case SH_NET_EVT_CLIENT_LEFT:
        if (n->mode == SH_NET_AP && n->clients > 0 && --n->clients == 0) {
            n->idle_s = now_s;
        }
        break;
    case SH_NET_EVT_AP_TIMEOUT:
        if (n->mode == SH_NET_AP) {
            return ap_over(n, has_network, true);
        }
        break;
    case SH_NET_EVT_PORTAL_TIMEOUT:
        if (n->mode == SH_NET_CP) {
            n->radio_off = true;
        }
        break;
    }
    return SH_NET_ACT_NONE;
}

sh_net_act_t sh_net_step(sh_net_t *n, bool updating, bool has_network, uint32_t now_s)
{
    if (n->ap_pending && !updating) {
        return start_ap(n, now_s);
    }
    if (n->mode == SH_NET_AP && n->clients == 0 && now_s - n->idle_s >= SH_NET_AP_IDLE_S) {
        return ap_over(n, has_network, false);
    }
    return SH_NET_ACT_NONE;
}

const char *sh_net_code(const char *ap_ssid)
{
    const size_t len = strlen(ap_ssid);
    return len > SH_NET_CODE_LEN ? ap_ssid + len - SH_NET_CODE_LEN : ap_ssid;
}

void sh_net_hostname(char *out, size_t size, const char *ap_ssid)
{
    if (size == 0) {
        return;
    }
    size_t i = 0;
    for (; ap_ssid[i] && i < size - 1; i++) {
        const char c = ap_ssid[i];
        out[i] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
    }
    out[i] = '\0';
}
