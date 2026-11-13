/* Created by Aram Vartanyan (c) 2026 */

/* Firmware update — when it is checked, offered and started (doc/web-interface.md 5) —
 * plain C, no ESP-IDF / FreeRTOS, compiled into the host test too. sh_net runs general-ota as these
 * rules ask and feeds its results back.
 *
 *   - one automatic check after boot: 1 min after boot if the device is on the router (CP) then;
 *     if the connection comes later (a slow router after a power cut, a return from AP) — 1 min
 *     after it. It is made once: losing and getting the connection again makes no new check, and
 *     there is none in AP. A failed automatic check is only logged;
 *   - a newer version found by it is offered on the display: "FUp" for 5 s, a short "menu" confirms;
 *     without the confirmation nothing is installed;
 *   - by hand from the web interface: "check", then "update" for a found version;
 *   - a confirmed update starts when the device is on the router, the pump is stopped, no protection
 *     is active (Hot, Fro, E4, E5) and no air venting runs; otherwise it waits ("FUp" among the
 *     labels of the normal screen until the update is over);
 *   - the pump logic goes on during the update; a failed update leaves the running version.
 * Times in ms of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_OTA_BOOT_CHECK_MS    60000u          /* the automatic check: 1 min after boot */
#define SH_OTA_OFFER_MS         5000u           /* "FUp" on the display */
#define SH_OTA_VERSION_MAX      32              /* a version with its NUL (general-ota) */

typedef enum {
    SH_OTA_IDLE = 0,            /* nothing checked yet */
    SH_OTA_CHECKING,
    SH_OTA_CURRENT,             /* last check: the running version is the newest */
    SH_OTA_AVAILABLE,           /* last check: `version` is newer */
    SH_OTA_WAITING,             /* confirmed: waits for the pump / the protection / the router */
    SH_OTA_RUNNING,             /* downloading and writing, `pct` */
    SH_OTA_DONE,                /* written: the restart follows */
    SH_OTA_NO_SERVER,           /* a check by hand failed: no connection to the update server */
    SH_OTA_FAILED,              /* the update failed: the running version stays */
} sh_ota_phase_t;

typedef enum {
    SH_OTA_CMD_CHECK,           /* web interface: "check for update" */
    SH_OTA_CMD_UPDATE,          /* web interface: "update" */
    SH_OTA_CMD_CONFIRM,         /* short "menu" while "FUp" is offered */
} sh_ota_cmd_t;

/* Results of general-ota. */
typedef enum {
    SH_OTA_EVT_CURRENT,         /* the server has nothing newer */
    SH_OTA_EVT_AVAILABLE,       /* `version` is newer */
    SH_OTA_EVT_PROGRESS,        /* `pct` (-1: size unknown) */
    SH_OTA_EVT_SUCCESS,
    SH_OTA_EVT_FAILED,
} sh_ota_evt_t;

/* What sh_net has to do. */
typedef enum {
    SH_OTA_ACT_NONE = 0,
    SH_OTA_ACT_CHECK,           /* general_ota_check() */
    SH_OTA_ACT_UPDATE,          /* general_ota_update() */
} sh_ota_act_t;

typedef struct {
    bool online;                /* CP and connected to the router */
    bool pump_on;
    uint8_t alarms;             /* SH_ALARM_BIT() of sh_alarm_t */
    bool air;                   /* the air venting runs (also in its pauses) */
} sh_ota_in_t;

/* All zeros at boot. */
typedef struct {
    sh_ota_phase_t phase;
    bool boot_done;             /* the automatic check has been made */
    bool minute;                /* the first minute after boot is over */
    bool was_online;            /* on the router at the last step ... */
    uint32_t online_ms;         /* ... since */
    bool auto_check;            /* the running check is the automatic one */
    bool offer;                 /* "FUp" is on the display ... */
    uint32_t offer_ms;          /* ... since */
    int8_t pct;                 /* progress of the update, -1: unknown */
    char version[SH_OTA_VERSION_MAX];   /* the newer version, "" none */
} sh_ota_t;

/* A confirmed update may start now. */
bool sh_ota_may_start(const sh_ota_in_t *in);

/* Every 200 ms or so: the automatic check, the end of the offer, the start of a waiting update. */
sh_ota_act_t sh_ota_step(sh_ota_t *o, const sh_ota_in_t *in, uint32_t now_ms);

/* A command of the web interface or of the "menu" button. */
sh_ota_act_t sh_ota_cmd(sh_ota_t *o, sh_ota_cmd_t cmd, const sh_ota_in_t *in, uint32_t now_ms);

/* A result of general-ota (`version` may be NULL). */
void sh_ota_event(sh_ota_t *o, sh_ota_evt_t evt, const char *version, int pct, uint32_t now_ms);

/* The update is being written (or its restart is pending): white blinking LED, AP is postponed. */
bool sh_ota_updating(const sh_ota_t *o);

/* "FUp" among the labels of the normal screen: a confirmed update is waiting or running. */
bool sh_ota_label(const sh_ota_t *o);
