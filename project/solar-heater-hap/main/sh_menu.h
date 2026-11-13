/* User menu on the buttons and the display (doc/operating-modes.md 2.5;
 * doc/tasks/step2-settings-menu-pump-sensors.md 3.2) — the transitions in plain C, compiled into the
 * host test too. sh_buttons feeds the keys, sh_display shows the view.
 *
 *   normal screen   "+" pump test (during the test +60 s), "-" during the test -60 s,
 *                   "menu" -> the list at SSn (during the test: stops the test; during the air
 *                   venting: stops it, and "+" does not start a test)
 *   list            SSn, HoL, tbo, C.Lo, d.on, d.oF, tSt, nEt, HAP, End; "+" / "-" next / previous item,
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
 *   HAP             the setting "Apple HomeKit" (group F): "on" / "oFF" ("+" on, "-" oFF); "menu"
 *                   confirms: a different value is passed on (it is stored and the controller
 *                   restarts); oFF while a controller is paired is refused: "on" steady for 1 s,
 *                   nothing is passed on
 *   10 s without a key in the list or on a value -> normal screen, an unconfirmed value is lost.
 * While "FUp" is offered on the normal screen (a newer firmware, 5 s) a short "menu" confirms the
 * update instead of anything else.
 * Child lock (Lock Physical Controls of HomeKit): on the normal screen every short press — the
 * way into the menu, the pump test and whatever else the keys do there — only shows "LoC" for 1 s.
 * The resets by holding "menu" are not keys of the menu and stay.
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
    SH_MENU_HAP,                /* Apple HomeKit: on / oFF (kept by sh_hap; a change restarts) */
    SH_MENU_END,                /* the item after the settings */
    SH_MENU_ITEMS
} sh_menu_item_t;

/* The setting behind an item (not for SH_MENU_NET, SH_MENU_HAP and SH_MENU_END); `winter`: the
 * current season. */
sh_setting_t sh_menu_setting(uint8_t pos, bool winter);
#define SH_MENU_TIMEOUT_MS      10000u
#define SH_MENU_SAVED_MS        1000u
#define SH_MENU_BLINK_MS        500u                /* on 0.5 s, off 0.5 s */
#define SH_MENU_CODE_MS         2000u               /* nEt -> AP: each half of the access code */
#define SH_MENU_LOCKED_MS       1000u               /* "LoC" after a key with the child lock on */

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
    SH_MENU_ACT_HAP,            /* Apple HomeKit switched: `value` 0 off / 1 on (store, restart) */
} sh_menu_act_t;

typedef struct {
    sh_menu_mode_t mode;
    uint8_t pos;                /* item: sh_menu_item_t */
    uint8_t id;                 /* the setting being edited / just saved (sh_setting_t) */
    int16_t value;              /* value being edited / just saved */
    uint32_t key_ms;            /* last key (the 10 s timeout) */
    uint32_t mark_ms;           /* start of the blinking / of the 1 s steady value */
    bool locked;                /* "LoC" is shown (a key met the child lock) ... */
    uint32_t locked_ms;         /* ... since */
} sh_menu_t;

typedef struct {
    sh_menu_mode_t mode;
    uint8_t pos;
    int16_t value;
    bool value_visible;         /* false in the "off" half of the blinking */
    uint8_t code_part;          /* nEt -> AP confirmed: 1 / 2 the half of the access code, 0 none */
    bool locked;                /* normal screen: "LoC" instead of everything else */
} sh_menu_view_t;

/* The timeouts up to `now_ms` (1 s steady value -> list, 10 s without a key -> normal screen, 1 s
 * of "LoC"). */
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
    bool hap_paired;            /* a HomeKit controller is paired: HAP cannot be switched off */
    bool locked;                /* the child lock is on */
} sh_menu_ctx_t;

/* One key. `s`: the current settings. */
sh_menu_act_t sh_menu_key(sh_menu_t *m, sh_key_t key, uint32_t now_ms, const sh_menu_ctx_t *ctx,
                          const sh_settings_t *s);

/* What the display shows (call sh_menu_update() first). */
void sh_menu_view(const sh_menu_t *m, uint32_t now_ms, sh_menu_view_t *out);

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
