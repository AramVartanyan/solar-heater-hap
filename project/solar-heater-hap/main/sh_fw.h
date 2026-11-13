/* Firmware update: when it is checked, asked for and started (doc/web-interface.md 5;
 * doc/tasks/step7-network-ota.md 3.3) — plain C, no ESP-IDF / FreeRTOS, compiled into the host test
 * too. sh_link runs it with general-ota.
 *
 *   automatic check  once after the restart, in CP with the connection to the router: 1 min after the
 *                    restart, or 1 min after the connection when it comes later; a failure only in the
 *                    log
 *   a newer version  after the automatic check: "FUp" on the display for 5 s; a short "menu" in that
 *                    time confirms, otherwise nothing is installed
 *   by hand          from the web interface (the code): check, and "Update" when a newer version is there
 *   the start        a confirmed update starts when the pump is off and no protection is active
 *                    (Hot, Fro, E4, E5); until then it waits ("FUp" on the display)
 *   the end          written -> restart into the new version; failed -> the old one stays (the other OTA
 *                    partition is not activated)
 * Times: monotonic seconds. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_FW_AUTO_AFTER_S      60u     /* the automatic check after the restart / the connection */
#define SH_FW_PROMPT_S          5u      /* "FUp" waits for the confirmation */
#define SH_FW_VER_LEN           32

typedef enum {
    SH_FW_IDLE = 0,             /* not checked yet */
    SH_FW_CHECKING,
    SH_FW_CURRENT,              /* the last check: up to date */
    SH_FW_AVAILABLE,            /* the last check: `ver` is newer */
    SH_FW_NO_SERVER,            /* the last check failed: no connection to the update server */
    SH_FW_WAITING,              /* confirmed: waits for the pump and the protections */
    SH_FW_UPDATING,             /* downloading and writing */
    SH_FW_FAILED,               /* the update failed: the old version runs */
    SH_FW_DONE,                 /* written: restart */
} sh_fw_state_t;

typedef enum {
    SH_FW_RES_CURRENT = 0,
    SH_FW_RES_AVAILABLE,
    SH_FW_RES_ERROR,
} sh_fw_result_t;

typedef struct {
    sh_fw_state_t state;
    bool auto_done;             /* the automatic check has been started */
    bool auto_check;            /* the running check is the automatic one */
    bool linked;                /* CP with the connection, since ... */
    uint32_t linked_s;
    uint32_t prompt_until_s;    /* "FUp" for the confirmation until (0: none) */
    uint8_t pct;                /* the progress of the update */
    char ver[SH_FW_VER_LEN];    /* the newer version on the server */
} sh_fw_t;

/* The connection to the router in CP came (true) or went (false). */
void sh_fw_link(sh_fw_t *f, bool linked, uint32_t now_s);

/* Once a second: true when the automatic check is due now (it is marked as started). */
bool sh_fw_auto_due(sh_fw_t *f, uint32_t now_s);

/* A check started (automatic or by hand); false if another operation is running. */
bool sh_fw_check_start(sh_fw_t *f, bool automatic);

/* The result of a check; a newer version after the automatic check asks for the confirmation. */
void sh_fw_check_done(sh_fw_t *f, sh_fw_result_t res, const char *ver, uint32_t now_s);

/* "FUp" asks for the confirmation now. */
bool sh_fw_prompt(const sh_fw_t *f, uint32_t now_s);

/* Short "menu" during the prompt, or "Update" in the web interface (a newer version found):
 * the update waits for its start. False if there is nothing to confirm. */
bool sh_fw_confirm(sh_fw_t *f, uint32_t now_s, bool from_menu);

/* The update may start: the pump is off and no protection is active (alarm bits of sh_alarm_t). */
bool sh_fw_may_start(bool pump_on, uint8_t alarms);

/* Once a second while waiting: true when the update starts now. */
bool sh_fw_step(sh_fw_t *f, bool pump_on, uint8_t alarms);

/* The progress, and the end of the update. */
void sh_fw_progress(sh_fw_t *f, int pct);
void sh_fw_update_done(sh_fw_t *f, bool ok);

/* A check or an update is busy (a choice of AP is deferred meanwhile). */
bool sh_fw_busy(const sh_fw_t *f);

/* The key of the state for the web interface. */
const char *sh_fw_state_key(sh_fw_state_t st);
