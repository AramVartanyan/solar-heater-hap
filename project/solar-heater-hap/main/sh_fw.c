/* Firmware update — see sh_fw.h. */

#include <stdio.h>
#include "sh_alarm.h"
#include "sh_fw.h"

void sh_fw_link(sh_fw_t *f, bool linked, uint32_t now_s)
{
    if (linked && !f->linked) {
        f->linked_s = now_s;
    }
    f->linked = linked;
}

bool sh_fw_auto_due(sh_fw_t *f, uint32_t now_s)
{
    if (f->auto_done || !f->linked || now_s < SH_FW_AUTO_AFTER_S) {
        return false;
    }
    /* connected within the first minute: at 1 min; later: 1 min after the connection */
    if (f->linked_s >= SH_FW_AUTO_AFTER_S && now_s - f->linked_s < SH_FW_AUTO_AFTER_S) {
        return false;
    }
    if (sh_fw_busy(f) || f->state == SH_FW_DONE) {
        f->auto_done = true;                /* a check by hand came first */
        return false;
    }
    f->auto_done = true;
    return true;
}

bool sh_fw_check_start(sh_fw_t *f, bool automatic)
{
    if (sh_fw_busy(f) || f->state == SH_FW_DONE) {
        return false;
    }
    f->state = SH_FW_CHECKING;
    f->auto_check = automatic;
    f->prompt_until_s = 0;
    return true;
}

void sh_fw_check_done(sh_fw_t *f, sh_fw_result_t res, const char *ver, uint32_t now_s)
{
    if (f->state != SH_FW_CHECKING) {
        return;
    }
    switch (res) {
    case SH_FW_RES_AVAILABLE:
        f->state = SH_FW_AVAILABLE;
        snprintf(f->ver, sizeof(f->ver), "%s", ver ? ver : "");
        if (f->auto_check) {
            f->prompt_until_s = now_s + SH_FW_PROMPT_S;
        }
        break;
    case SH_FW_RES_CURRENT:
        f->state = SH_FW_CURRENT;
        break;
    case SH_FW_RES_ERROR:
    default:
        f->state = SH_FW_NO_SERVER;
        break;
    }
    f->auto_check = false;
}

bool sh_fw_prompt(const sh_fw_t *f, uint32_t now_s)
{
    return f->state == SH_FW_AVAILABLE && f->prompt_until_s != 0 && now_s < f->prompt_until_s;
}

bool sh_fw_confirm(sh_fw_t *f, uint32_t now_s, bool from_menu)
{
    if (f->state != SH_FW_AVAILABLE || (from_menu && !sh_fw_prompt(f, now_s))) {
        return false;
    }
    f->state = SH_FW_WAITING;
    f->prompt_until_s = 0;
    return true;
}

bool sh_fw_may_start(bool pump_on, uint8_t alarms)
{
    return !pump_on && !(alarms & (SH_ALARM_PROTECT | SH_ALARM_OVERHEAT));
}

bool sh_fw_step(sh_fw_t *f, bool pump_on, uint8_t alarms)
{
    if (f->state != SH_FW_WAITING || !sh_fw_may_start(pump_on, alarms)) {
        return false;
    }
    f->state = SH_FW_UPDATING;
    f->pct = 0;
    return true;
}

void sh_fw_progress(sh_fw_t *f, int pct)
{
    if (f->state == SH_FW_UPDATING && pct >= 0 && pct <= 100) {
        f->pct = (uint8_t)pct;
    }
}

void sh_fw_update_done(sh_fw_t *f, bool ok)
{
    if (f->state == SH_FW_UPDATING) {
        f->state = ok ? SH_FW_DONE : SH_FW_FAILED;
        if (ok) {
            f->pct = 100;
        }
    }
}

bool sh_fw_busy(const sh_fw_t *f)
{
    return f->state == SH_FW_CHECKING || f->state == SH_FW_WAITING || f->state == SH_FW_UPDATING;
}

const char *sh_fw_state_key(sh_fw_state_t st)
{
    static const char *const keys[] = {
        [SH_FW_IDLE] = "idle", [SH_FW_CHECKING] = "check", [SH_FW_CURRENT] = "current",
        [SH_FW_AVAILABLE] = "avail", [SH_FW_NO_SERVER] = "noserver", [SH_FW_WAITING] = "wait",
        [SH_FW_UPDATING] = "run", [SH_FW_FAILED] = "fail", [SH_FW_DONE] = "done",
    };
    return (unsigned)st < sizeof(keys) / sizeof(keys[0]) ? keys[st] : "idle";
}
