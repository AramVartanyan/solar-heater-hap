/* Firmware update rules — see sh_ota.h. */

#include <stdio.h>
#include "sh_alarm.h"
#include "sh_ota.h"

bool sh_ota_may_start(const sh_ota_in_t *in)
{
    return in->online && !in->pump_on && !in->air &&
           (in->alarms & (SH_ALARM_PROTECT | SH_ALARM_OVERHEAT)) == 0;
}

static bool busy(const sh_ota_t *o)
{
    return o->phase == SH_OTA_CHECKING || o->phase == SH_OTA_WAITING || o->phase == SH_OTA_RUNNING ||
           o->phase == SH_OTA_DONE;
}

/* A waiting update starts as soon as it may. */
static sh_ota_act_t try_start(sh_ota_t *o, const sh_ota_in_t *in)
{
    if (o->phase != SH_OTA_WAITING || !sh_ota_may_start(in)) {
        return SH_OTA_ACT_NONE;
    }
    o->phase = SH_OTA_RUNNING;
    o->pct = 0;
    return SH_OTA_ACT_UPDATE;
}

sh_ota_act_t sh_ota_step(sh_ota_t *o, const sh_ota_in_t *in, uint32_t now_ms)
{
    if (o->offer && now_ms - o->offer_ms >= SH_OTA_OFFER_MS) {
        o->offer = false;
    }
    if (!o->boot_done && now_ms >= SH_OTA_BOOT_CHECK_MS) {
        o->boot_done = true;            /* once: not later if the device is not on the router now */
        if (in->online && !busy(o)) {
            o->phase = SH_OTA_CHECKING;
            o->auto_check = true;
            return SH_OTA_ACT_CHECK;
        }
    }
    return try_start(o, in);
}

sh_ota_act_t sh_ota_cmd(sh_ota_t *o, sh_ota_cmd_t cmd, const sh_ota_in_t *in, uint32_t now_ms)
{
    (void)now_ms;
    switch (cmd) {
    case SH_OTA_CMD_CHECK:
        if (!in->online || busy(o)) {
            return SH_OTA_ACT_NONE;
        }
        o->phase = SH_OTA_CHECKING;
        o->auto_check = false;
        o->offer = false;
        return SH_OTA_ACT_CHECK;
    case SH_OTA_CMD_CONFIRM:            /* only while "FUp" is offered */
    case SH_OTA_CMD_UPDATE:
        if ((cmd == SH_OTA_CMD_CONFIRM && !o->offer) || o->phase != SH_OTA_AVAILABLE) {
            return SH_OTA_ACT_NONE;
        }
        o->phase = SH_OTA_WAITING;
        return try_start(o, in);
    }
    return SH_OTA_ACT_NONE;
}

void sh_ota_event(sh_ota_t *o, sh_ota_evt_t evt, const char *version, int pct, uint32_t now_ms)
{
    const bool checking = o->phase == SH_OTA_CHECKING, running = o->phase == SH_OTA_RUNNING;
    switch (evt) {
    case SH_OTA_EVT_CURRENT:
        if (checking || running) {      /* running: the server image was replaced meanwhile */
            o->phase = SH_OTA_CURRENT;
            o->version[0] = '\0';
        }
        break;
    case SH_OTA_EVT_AVAILABLE:
        if (checking || running) {
            snprintf(o->version, sizeof(o->version), "%s", version ? version : "");
        }
        if (checking) {
            o->phase = SH_OTA_AVAILABLE;
            if (o->auto_check) {        /* only the automatic check offers it on the display */
                o->offer = true;
                o->offer_ms = now_ms;
            }
        }
        break;
    case SH_OTA_EVT_PROGRESS:
        if (running) {
            o->pct = (int8_t)(pct < 0 ? -1 : pct > 100 ? 100 : pct);
        }
        break;
    case SH_OTA_EVT_SUCCESS:
        if (running) {
            o->phase = SH_OTA_DONE;
            o->pct = 100;
        }
        break;
    case SH_OTA_EVT_FAILED:
        if (checking) {
            /* the automatic check: only the log; by hand: "no connection to the update server" */
            o->phase = o->auto_check ? SH_OTA_IDLE : SH_OTA_NO_SERVER;
        } else if (running) {
            o->phase = SH_OTA_FAILED;
        }
        break;
    }
    if (o->phase != SH_OTA_CHECKING) {
        o->auto_check = false;
    }
}

bool sh_ota_updating(const sh_ota_t *o)
{
    return o->phase == SH_OTA_RUNNING || o->phase == SH_OTA_DONE;
}

bool sh_ota_label(const sh_ota_t *o)
{
    return o->phase == SH_OTA_WAITING || sh_ota_updating(o);
}
