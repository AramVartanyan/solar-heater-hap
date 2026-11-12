/* User menu on the three buttons and the 3-digit display (doc/operating-modes.md 2.5): the
 * transitions and what the display shows — plain C, no ESP-IDF / FreeRTOS, compiled into the host
 * test too. Times are milliseconds of a monotonic clock (differences only, so a wrap is harmless).
 *
 *   normal screen --menu--> list: tbo, C.Lo, d.on, d.oF, tSt, End   ("+" / "-": next / previous,
 *                                                                    round and round)
 *   list --menu on a parameter--> value, blinking 0.5 s / 0.5 s      ("+" / "-": one step, inside
 *                                                                    the range, no wrap)
 *   list --menu on End--> normal screen
 *   value --menu--> stored if it differs; shown steady for 1 s, then the list at the same position
 *   10 s without a key in the list or on a value --> normal screen, a changed value is dropped
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sh_settings_logic.h"

#define SH_MENU_DIGITS      3
#define SH_MENU_POS_END     SH_SET_COUNT    /* the list: one position per setting, then "End" */
#define SH_MENU_POS_COUNT   (SH_SET_COUNT + 1)

#define SH_MENU_TIMEOUT_MS  10000           /* without a key: back to the normal screen */
#define SH_MENU_SAVED_MS    1000            /* the confirmed value, steady */
#define SH_MENU_BLINK_MS    500             /* value on / off */

/* Pump test on the display: "tSt" for 3 s, then the time left; "tSt" again for 1 s every 7 s. */
#define SH_MENU_TEST_LABEL_MS   3000
#define SH_MENU_TEST_PERIOD_MS  7000
#define SH_MENU_TEST_AGAIN_MS   1000

typedef enum {
    SH_KEY_MENU,                /* short presses */
    SH_KEY_PLUS,
    SH_KEY_MINUS,
    SH_KEY_PLUS_REPEAT,         /* "+" / "-" held: auto-repeat, used on a value only */
    SH_KEY_MINUS_REPEAT,
} sh_key_t;

typedef enum {
    SH_MENU_OFF = 0,            /* normal screen */
    SH_MENU_LIST,
    SH_MENU_EDIT,               /* value blinking */
    SH_MENU_SAVED,              /* confirmed value, steady for 1 s */
} sh_menu_state_t;

/* All zeros at start: the normal screen. */
typedef struct {
    sh_menu_state_t state;
    uint8_t  pos;               /* position in the list */
    int16_t  value;             /* the value being changed / just confirmed */
    uint32_t key_ms;            /* the last key */
    uint32_t mark_ms;           /* start of the blinking / of the steady second */
} sh_menu_t;

/* A key at `now_ms`; `cur` are the settings in use. Returns true when a changed value was
 * confirmed: the caller stores `*value` as setting `*id`. On the normal screen only "menu" does
 * something here ("+" belongs to the pump test). */
bool sh_menu_key(sh_menu_t *m, sh_key_t key, uint32_t now_ms, const sh_settings_t *cur,
                 sh_setting_id_t *id, int16_t *value);

/* Time passing: the 10 s timeout and the end of the steady second. Call before showing. */
void sh_menu_tick(sh_menu_t *m, uint32_t now_ms);

/* What the display shows in the menu (not for SH_MENU_OFF), as raw segments XGFEDCBA. */
void sh_menu_render(const sh_menu_t *m, uint32_t now_ms, uint8_t seg[SH_MENU_DIGITS]);

/* A whole number 0 - 999, right aligned, as raw segments. */
void sh_menu_number(int value, uint8_t seg[SH_MENU_DIGITS]);

/* The label "tSt" as raw segments. */
void sh_menu_test_label(uint8_t seg[SH_MENU_DIGITS]);

/* Pump test running for `elapsed_ms`: true while the label is shown, false for the time left. */
bool sh_menu_test_shows_label(uint32_t elapsed_ms);
