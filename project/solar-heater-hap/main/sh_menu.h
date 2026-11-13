/* User menu on the buttons and the display (doc/operating-modes.md 2.5;
 * doc/tasks/step2-settings-menu-pump-sensors.md 3.2) — the transitions in plain C, compiled into the
 * host test too. sh_buttons feeds the keys, sh_display shows the view.
 *
 *   normal screen   "+" pump test (during the test +60 s), "-" during the test -60 s,
 *                   "menu" -> the list at SSn (during the test: stops the test; during the air
 *                   venting: stops it, and "+" does not start a test)
 *   list            SSn, HoL, tbo, C.Lo, d.on, d.oF, tSt, nEt, HAP, LoC, End; "+" / "-" next / previous item,
 *                   round (after End -> SSn); C.Lo, d.on and d.oF are those of the current season;
 *                   "menu" on a setting -> its value blinks; on End -> normal screen
 *   value (blinks)  "+" / "-" one step within the range (no wrap), held > 1 s: repeated;
 *                   "menu" confirms: saved only if different, the value shown steady for 1 s,
 *                   then the list at the same item
 *   nEt             the network mode (group E): the value is the current mode, "CP" / "AP" ("+" AP,
 *                   "-" CP); "menu" confirms — always passed on (CP switches a radio that is off on
 *                   again); after AP the access code is shown in two parts, 2 s each ("810", "840"),
 *                   instead of the steady value; AP while the device is connected to the router is
 *                   refused: "CP" steady for 1 s, nothing is passed on; the mode is not stored
 *   HAP             the setting "Apple HomeKit" (group F): "on" / "oFF"; a change is passed on (stored,
 *                   then the controller restarts); "oFF" while paired is refused: "on" steady for 1 s
 *   LoC             the keypad lock (group F): opens always at a blinking "oFF"; "on" + "menu" locks
 *                   (the menu closes); "oFF" + "menu" changes nothing
 *   10 s without a key in the list or on a value -> normal screen, an unconfirmed value is lost.
 * Keypad lock (group F; the same variable is Lock Physical Controls in HomeKit and the switch of the
 * web interface): while locked every key does nothing — the menu, the pump test, stopping the air
 * venting, the confirmation of "FUp" and the holds of "menu" (5 s Wi-Fi reset, 9 s factory reset);
 * a locked key shows "LoC" for 1 s. "+" and "-" held together for 3 s unlock it, whatever is shown;
 * unlocked, the same locks it, but only from the normal screen without the pump test / air venting
 * (in the menu and during the test holding both does nothing: no value change, no lock, no test).
 * A change of the lock from any source shows "LoC" (locked) / "UnL" (unlocked) for 1 s; then the
 * current view goes on (the test and the air venting are not interrupted); locking closes an open
 * menu (an unconfirmed value is lost).
 * Units (group F): the menu values are kept in °C / K; with °F the display shows tbo and C.Lo in °F
 * and d.on, d.oF x 1.8 (sh_menu_value_shown()); the step stays 1 °C / 1 K.
 * While "FUp" is offered on the normal screen (a newer firmware, 5 s) a short "menu" confirms the
 * update instead of anything else.
 * Times in ms of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_settings.h"

/* The items of the list, in their order. */
typedef enum {
    SH_MENU_SSN = 0,            /* season: Sun / Cld / Aut */
    SH_MENU_HOL,                /* vacation: on / oFF */
    SH_MENU_TBO,
    SH_MENU_CLO,
    SH_MENU_DON,
    SH_MENU_DOF,
    SH_MENU_TST,
    SH_MENU_NET,                /* network mode: CP / AP (not a setting of sh_settings) */
    SH_MENU_HAP,                /* the setting "Apple HomeKit": on / oFF (sh_hap) */
    SH_MENU_LOC,                /* the keypad lock: oFF / on */
    SH_MENU_END,                /* the item after the settings */
    SH_MENU_ITEMS
} sh_menu_item_t;

/* The setting behind an item (not for SH_MENU_NET and SH_MENU_END); `winter`: the current season. */
sh_setting_t sh_menu_setting(uint8_t pos, bool winter);
#define SH_MENU_TIMEOUT_MS      10000u
#define SH_MENU_SAVED_MS        1000u
#define SH_MENU_BLINK_MS        500u                /* on 0.5 s, off 0.5 s */
#define SH_MENU_CODE_MS         2000u               /* nEt -> AP: each half of the access code */
#define SH_MENU_NOTICE_MS       1000u               /* "LoC" / "UnL" */
#define SH_MENU_COMBO_MS        3000u               /* "+" and "-" held together: lock / unlock */

/* Pump test screen: "tSt" for 3 s, then the seconds left, "tSt" again for 1 s every 7 s. */
#define SH_MENU_TEST_LABEL_S    3u
#define SH_MENU_TEST_PERIOD_S   7u

/* Air venting screen: "AIr" for 3 s, then periods of 7 s: the phase ("run" / "PAu") 1 s, the minutes
 * left of the whole venting 5 s, "AIr" 1 s. */
typedef enum {
    SH_MENU_AIR_LABEL = 0,      /* "AIr" */
    SH_MENU_AIR_PHASE,          /* "run" / "PAu" */
    SH_MENU_AIR_LEFT,           /* minutes left */
} sh_menu_air_t;
sh_menu_air_t sh_menu_air_screen(uint32_t elapsed_s);

typedef enum {
    SH_MENU_NORMAL = 0,         /* normal screen (temperatures or the pump test) */
    SH_MENU_LIST,               /* item name */
    SH_MENU_EDIT,               /* value, blinking */
    SH_MENU_SAVED,              /* value, steady for 1 s after the confirmation */
} sh_menu_mode_t;

typedef enum {
    SH_KEY_MENU,                /* short press */
    SH_KEY_PLUS,
    SH_KEY_MINUS,
    SH_KEY_PLUS_REPEAT,         /* held > 1 s, every 200 ms */
    SH_KEY_MINUS_REPEAT,
} sh_key_t;

typedef enum {
    SH_MENU_ACT_NONE = 0,
    SH_MENU_ACT_PLUS,           /* pump test: start, or +60 s */
    SH_MENU_ACT_MINUS,          /* pump test: -60 s */
    SH_MENU_ACT_STOP,           /* pump test: stop */
    SH_MENU_ACT_SAVE,           /* store `value` as setting `id` */
    SH_MENU_ACT_AIR_STOP,       /* air venting: stop */
    SH_MENU_ACT_NET,            /* network mode chosen: `value` 0 CP / 1 AP */
    SH_MENU_ACT_FUP,            /* the offered firmware update is confirmed */
    SH_MENU_ACT_HAP,            /* "Apple HomeKit" changed: `value` 0 off / 1 on (store, restart) */
    SH_MENU_ACT_LOCK,           /* lock the keypad */
    SH_MENU_ACT_UNLOCK,         /* unlock the keypad */
} sh_menu_act_t;

typedef enum {
    SH_MENU_NOTICE_NONE = 0,
    SH_MENU_NOTICE_LOC,         /* "LoC": locked, or a locked key */
    SH_MENU_NOTICE_UNL,         /* "UnL": unlocked */
} sh_menu_notice_t;

typedef struct {
    sh_menu_mode_t mode;
    uint8_t pos;                /* item: sh_menu_item_t */
    uint8_t id;                 /* the setting being edited / just saved (sh_setting_t) */
    int16_t value;              /* value being edited / just saved */
    uint32_t key_ms;            /* last key (the 10 s timeout) */
    uint32_t mark_ms;           /* start of the blinking / of the 1 s steady value */
    /* the keypad lock */
    bool down[2];               /* "-", "+" are held */
    bool both;                  /* both held ... */
    uint32_t both_ms;           /* ... since */
    bool combo_done;            /* the 3 s of this hold have acted */
    bool chord;                 /* "+" / "-" belong to a hold of both: their taps and repeats are ignored */
    bool locked;                /* the lock as last seen (sh_menu_sync_lock()) */
    bool synced;                /* ... seen at least once */
    uint8_t notice;             /* sh_menu_notice_t ... */
    uint32_t notice_ms;         /* ... since */
} sh_menu_t;

typedef struct {
    sh_menu_mode_t mode;
    uint8_t pos;
    int16_t value;
    bool value_visible;         /* false in the "off" half of the blinking */
    uint8_t code_part;          /* nEt -> AP confirmed: 1 / 2 the half of the access code, 0 none */
    uint8_t notice;             /* sh_menu_notice_t: "LoC" / "UnL" over everything for 1 s */
} sh_menu_view_t;

/* The timeouts up to `now_ms` (1 s steady value -> list, 10 s without a key -> normal screen). */
void sh_menu_update(sh_menu_t *m, uint32_t now_ms);

/* What runs on the normal screen and which season it is. */
typedef struct {
    bool test_running;          /* the pump test is on */
    bool air_running;           /* the air venting is on */
    bool winter;                /* the current season */
    bool net_ap;                /* the network mode is AP */
    bool net_connected;         /* connected to the router: AP cannot be chosen */
    bool fup_offer;             /* "FUp" is offered on the display */
    bool hap_on;                /* the setting "Apple HomeKit" */
    bool hap_paired;            /* paired with a controller: HAP cannot be switched off */
    bool locked;                /* the keypad is locked */
} sh_menu_ctx_t;

/* One key. `s`: the current settings. */
sh_menu_act_t sh_menu_key(sh_menu_t *m, sh_key_t key, uint32_t now_ms, const sh_menu_ctx_t *ctx,
                          const sh_settings_t *s);

/* "+" (`plus`) or "-" pressed (`down`) or released — for the hold of both. */
void sh_menu_button(sh_menu_t *m, bool plus, bool down, uint32_t now_ms);

/* Often (every 100 ms or so): the 3 s hold of "+" and "-" -> SH_MENU_ACT_LOCK / _UNLOCK. */
sh_menu_act_t sh_menu_poll(sh_menu_t *m, uint32_t now_ms, const sh_menu_ctx_t *ctx);

/* "menu" held for a reset (5 s / 9 s): false while locked (and "LoC" is shown). */
bool sh_menu_hold(sh_menu_t *m, uint32_t now_ms, const sh_menu_ctx_t *ctx);

/* The keypad lock as it is now, from any source: a change shows "LoC" / "UnL" for 1 s, locking
 * closes the menu. The first call only takes the value. */
void sh_menu_sync_lock(sh_menu_t *m, bool locked, uint32_t now_ms);

/* What the display shows (call sh_menu_update() first). */
void sh_menu_view(const sh_menu_t *m, uint32_t now_ms, sh_menu_view_t *out);

/* A menu value as the display shows it: tbo and C.Lo in °F, d.on and d.oF x 1.8 with `fahrenheit`,
 * the rest as it is. */
int16_t sh_menu_value_shown(uint8_t pos, int16_t value, bool fahrenheit);

/* Pump test screen, `elapsed_s` after the start of the test: true = "tSt", false = seconds left. */
bool sh_menu_test_label(uint32_t elapsed_s);

/* The labels of the normal screen after the alarm and information codes, 1 s each, in this order:
 * "HoL" in the vacation, "AP" while the own access point is on, "FUp" while a confirmed firmware
 * update waits or runs. `idx`: 0 for the first second after the codes; SH_MENU_LABEL_NONE when the
 * labels are over (the cycle starts again). */
typedef enum {
    SH_MENU_LABEL_NONE = 0,
    SH_MENU_LABEL_HOL,
    SH_MENU_LABEL_AP,
    SH_MENU_LABEL_FUP,
} sh_menu_label_t;
sh_menu_label_t sh_menu_label_at(bool vacation, bool ap, bool fup, uint32_t idx);
