/* User menu on the buttons and the display (doc/operating-modes.md 2.5;
 * doc/tasks/step2-settings-menu-pump-sensors.md 3.2) — the transitions in plain C, compiled into the
 * host test too. sh_buttons feeds the keys, sh_display shows the view.
 *
 *   normal screen   "+" pump test (during the test +60 s), "-" during the test -60 s,
 *                   "menu" -> the list at tbo (during the test: stops the test)
 *   list            "+" / "-" next / previous item, round (after End -> tbo);
 *                   "menu" on a setting -> its value blinks; on End -> normal screen
 *   value (blinks)  "+" / "-" one step within the range (no wrap), held > 1 s: repeated;
 *                   "menu" confirms: saved only if different, the value shown steady for 1 s,
 *                   then the list at the same item
 *   10 s without a key in the list or on a value -> normal screen, an unconfirmed value is lost.
 * Times in ms of a monotonic clock. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_settings.h"

#define SH_MENU_END             SH_SET_COUNT        /* the item after the settings */
#define SH_MENU_ITEMS           (SH_SET_COUNT + 1)
#define SH_MENU_TIMEOUT_MS      10000u
#define SH_MENU_SAVED_MS        1000u
#define SH_MENU_BLINK_MS        500u                /* on 0.5 s, off 0.5 s */

/* Pump test screen: "tSt" for 3 s, then the seconds left, "tSt" again for 1 s every 7 s. */
#define SH_MENU_TEST_LABEL_S    3u
#define SH_MENU_TEST_PERIOD_S   7u

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
    SH_MENU_ACT_SAVE,           /* store `value` as setting `pos` */
} sh_menu_act_t;

typedef struct {
    sh_menu_mode_t mode;
    uint8_t pos;                /* item: sh_setting_t or SH_MENU_END */
    int16_t value;              /* value being edited / just saved */
    uint32_t key_ms;            /* last key (the 10 s timeout) */
    uint32_t mark_ms;           /* start of the blinking / of the 1 s steady value */
} sh_menu_t;

typedef struct {
    sh_menu_mode_t mode;
    uint8_t pos;
    int16_t value;
    bool value_visible;         /* false in the "off" half of the blinking */
} sh_menu_view_t;

/* The timeouts up to `now_ms` (1 s steady value -> list, 10 s without a key -> normal screen). */
void sh_menu_update(sh_menu_t *m, uint32_t now_ms);

/* One key. `test_running`: the pump test is on; `s`: the current settings. */
sh_menu_act_t sh_menu_key(sh_menu_t *m, sh_key_t key, uint32_t now_ms, bool test_running,
                          const sh_settings_t *s);

/* What the display shows (call sh_menu_update() first). */
void sh_menu_view(const sh_menu_t *m, uint32_t now_ms, sh_menu_view_t *out);

/* Pump test screen, `elapsed_s` after the start of the test: true = "tSt", false = seconds left. */
bool sh_menu_test_label(uint32_t elapsed_s);
