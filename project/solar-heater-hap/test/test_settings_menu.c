/* Host test of the user settings (main/sh_settings_logic.c) and of the menu (main/sh_menu.c) — no
 * ESP-IDF needed.
 *
 *   make -C project/solar-heater-hap/test
 *
 * doc/operating-modes.md 2.5; doc/tasks/step2-settings-menu-pump-sensors.md 3.1, 3.2.
 * Times are milliseconds. */

#include <string.h>
#include "sh_settings_logic.h"
#include "sh_menu.h"
#include "test_util.h"

static const sh_settings_t DEF = { .v = { 70, 25, 6, 4, 300 } };
#define ALL     ((1u << SH_SET_COUNT) - 1)

/* ---- Settings ------------------------------------------------------------------------ */

static void test_settings_limits(void)
{
    static const sh_setting_range_t expect[SH_SET_COUNT] = {
        [SH_SET_TANK_MAX] = { 30, 85, 1 },
        [SH_SET_COL_MIN]  = { 10, 80, 1 },
        [SH_SET_DIFF_ON]  = { 3, 20, 1 },
        [SH_SET_DIFF_OFF] = { 1, 15, 1 },
        [SH_SET_TEST_S]   = { 60, 900, 30 },
    };
    for (int id = 0; id < SH_SET_COUNT; id++) {
        const sh_setting_range_t r = sh_setting_limits((sh_setting_id_t)id);
        CHECK(r.min == expect[id].min && r.max == expect[id].max && r.step == expect[id].step);
    }
}

static void test_settings_nothing_stored(void)
{
    sh_settings_t out, stored;
    memset(&stored, 0, sizeof(stored));
    sh_settings_check(&out, &stored, 0, &DEF);
    CHECK(memcmp(&out, &DEF, sizeof(out)) == 0);
}

static void test_settings_stored_values_used(void)
{
    const sh_settings_t stored = { .v = { 60, 15, 8, 3, 120 } };
    sh_settings_t out;
    sh_settings_check(&out, &stored, ALL, &DEF);
    CHECK(memcmp(&out, &stored, sizeof(out)) == 0);
}

static void test_settings_missing_value(void)
{
    const sh_settings_t stored = { .v = { 60, 15, 8, 3, 120 } };
    for (int id = 0; id < SH_SET_COUNT; id++) {
        if (id == SH_SET_DIFF_ON || id == SH_SET_DIFF_OFF) {
            continue;               /* the pair has its own test */
        }
        sh_settings_t out;
        sh_settings_check(&out, &stored, ALL & ~(1u << id), &DEF);
        for (int i = 0; i < SH_SET_COUNT; i++) {
            CHECK(out.v[i] == (i == id ? DEF.v[i] : stored.v[i]));
        }
    }
}

/* One stored value at `v`, the rest good: is it used, or replaced by the default? */
static bool stored_value_used(sh_setting_id_t id, int16_t v)
{
    sh_settings_t stored = { .v = { 60, 15, 20, 1, 120 } };     /* the differences never clash */
    stored.v[id] = v;
    sh_settings_t out;
    sh_settings_check(&out, &stored, ALL, &DEF);
    if (out.v[id] != v) {
        CHECK(out.v[id] == DEF.v[id]);
        return false;
    }
    return true;
}

static void test_settings_range_limits(void)
{
    for (int id = 0; id < SH_SET_COUNT; id++) {
        const sh_setting_range_t r = sh_setting_limits((sh_setting_id_t)id);
        CHECK(stored_value_used((sh_setting_id_t)id, r.min));
        CHECK(stored_value_used((sh_setting_id_t)id, r.max));
        CHECK(!stored_value_used((sh_setting_id_t)id, (int16_t)(r.min - 1)));
        CHECK(!stored_value_used((sh_setting_id_t)id, (int16_t)(r.max + 1)));
    }
    CHECK(!stored_value_used(SH_SET_TANK_MAX, -32768));
    CHECK(!stored_value_used(SH_SET_TEST_S, 32767));
}

static void test_settings_difference_pair(void)
{
    sh_settings_t out;

    sh_settings_t stored = { .v = { 60, 15, 5, 4, 120 } };     /* off = on - 1: allowed */
    sh_settings_check(&out, &stored, ALL, &DEF);
    CHECK(out.v[SH_SET_DIFF_ON] == 5 && out.v[SH_SET_DIFF_OFF] == 4);

    stored = (sh_settings_t){ .v = { 60, 15, 5, 5, 120 } };     /* off = on: both defaults */
    sh_settings_check(&out, &stored, ALL, &DEF);
    CHECK(out.v[SH_SET_DIFF_ON] == 6 && out.v[SH_SET_DIFF_OFF] == 4);
    CHECK(out.v[SH_SET_TANK_MAX] == 60 && out.v[SH_SET_TEST_S] == 120);    /* the others stay */

    stored = (sh_settings_t){ .v = { 60, 15, 5, 9, 120 } };     /* off > on */
    sh_settings_check(&out, &stored, ALL, &DEF);
    CHECK(out.v[SH_SET_DIFF_ON] == 6 && out.v[SH_SET_DIFF_OFF] == 4);

    /* One of the pair missing or out of range: its default may clash with the stored other one. */
    stored = (sh_settings_t){ .v = { 60, 15, 4, 3, 120 } };
    sh_settings_check(&out, &stored, ALL & ~(1u << SH_SET_DIFF_OFF), &DEF);    /* on 4, off default 4 */
    CHECK(out.v[SH_SET_DIFF_ON] == 6 && out.v[SH_SET_DIFF_OFF] == 4);

    stored = (sh_settings_t){ .v = { 60, 15, 21, 8, 120 } };    /* on out of range -> 6, off 8 */
    sh_settings_check(&out, &stored, ALL, &DEF);
    CHECK(out.v[SH_SET_DIFF_ON] == 6 && out.v[SH_SET_DIFF_OFF] == 4);

    stored = (sh_settings_t){ .v = { 60, 15, 12, 3, 120 } };
    sh_settings_check(&out, &stored, ALL & ~(1u << SH_SET_DIFF_OFF), &DEF);    /* on 12, off default 4 */
    CHECK(out.v[SH_SET_DIFF_ON] == 12 && out.v[SH_SET_DIFF_OFF] == 4);
}

static void test_settings_range_narrowed_by_the_other_difference(void)
{
    sh_settings_t s = DEF;          /* on 6, off 4 */
    sh_setting_range_t r = sh_setting_range(&s, SH_SET_DIFF_ON);
    CHECK(r.min == 5 && r.max == 20);
    r = sh_setting_range(&s, SH_SET_DIFF_OFF);
    CHECK(r.min == 1 && r.max == 5);

    s.v[SH_SET_DIFF_ON] = 20;       /* the fixed limit when the other one leaves room */
    s.v[SH_SET_DIFF_OFF] = 1;
    r = sh_setting_range(&s, SH_SET_DIFF_ON);
    CHECK(r.min == 3 && r.max == 20);
    r = sh_setting_range(&s, SH_SET_DIFF_OFF);
    CHECK(r.min == 1 && r.max == 15);

    s.v[SH_SET_DIFF_ON] = 16;
    s.v[SH_SET_DIFF_OFF] = 15;
    r = sh_setting_range(&s, SH_SET_DIFF_ON);
    CHECK(r.min == 16 && r.max == 20);
    r = sh_setting_range(&s, SH_SET_DIFF_OFF);
    CHECK(r.min == 1 && r.max == 15);

    for (int id = 0; id < SH_SET_COUNT; id++) {     /* the others: the fixed range */
        if (id != SH_SET_DIFF_ON && id != SH_SET_DIFF_OFF) {
            const sh_setting_range_t f = sh_setting_limits((sh_setting_id_t)id);
            r = sh_setting_range(&s, (sh_setting_id_t)id);
            CHECK(r.min == f.min && r.max == f.max && r.step == f.step);
        }
    }
}

/* ---- Menu ---------------------------------------------------------------------------- */

/* Raw segments, XGFEDCBA */
#define S_t 0x78
#define S_b 0x7C
#define S_o 0x5C
#define S_C 0x39
#define S_L 0x38
#define S_d 0x5E
#define S_n 0x54
#define S_F 0x71
#define S_S 0x6D
#define S_E 0x79
#define DOT 0x80
static const uint8_t D[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

/* The menu, the settings in use and a clock; values confirmed in the menu are stored, as
 * sh_display does it. */
typedef struct {
    sh_menu_t m;
    sh_settings_t set;
    uint32_t now_ms;
    int saves;                  /* how many times a value was stored */
} ui_t;

static ui_t ui_new(void)
{
    ui_t u;
    memset(&u, 0, sizeof(u));
    u.set = DEF;
    u.now_ms = 4000000000u;     /* the 32-bit clock wraps during the tests */
    return u;
}

static void wait(ui_t *u, uint32_t ms)
{
    u->now_ms += ms;
    sh_menu_tick(&u->m, u->now_ms);
}

static void key(ui_t *u, sh_key_t k)
{
    sh_setting_id_t id;
    int16_t value;
    if (sh_menu_key(&u->m, k, u->now_ms, &u->set, &id, &value)) {
        u->set.v[id] = value;
        u->saves++;
    }
}

/* A key 100 ms after the last thing that happened. */
static void press(ui_t *u, sh_key_t k)
{
    wait(u, 100);
    key(u, k);
}

static bool shows(const ui_t *u, uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t seg[SH_MENU_DIGITS];
    sh_menu_render(&u->m, u->now_ms, seg);
    return seg[0] == a && seg[1] == b && seg[2] == c;
}

/* In the list at position `pos`. */
static ui_t ui_at(int pos)
{
    ui_t u = ui_new();
    press(&u, SH_KEY_MENU);
    for (int i = 0; i < pos; i++) {
        press(&u, SH_KEY_PLUS);
    }
    CHECK(u.m.state == SH_MENU_LIST && u.m.pos == pos);
    return u;
}

/* Changing the value of the setting `id`. */
static ui_t ui_edit(sh_setting_id_t id)
{
    ui_t u = ui_at(id);
    press(&u, SH_KEY_MENU);
    CHECK(u.m.state == SH_MENU_EDIT);
    return u;
}

static void test_menu_normal_screen(void)
{
    ui_t u = ui_new();
    CHECK(u.m.state == SH_MENU_OFF);
    press(&u, SH_KEY_PLUS);         /* "+" is the pump test, "-" nothing: not the menu's keys */
    press(&u, SH_KEY_MINUS);
    press(&u, SH_KEY_PLUS_REPEAT);
    press(&u, SH_KEY_MINUS_REPEAT);
    CHECK(u.m.state == SH_MENU_OFF);
    press(&u, SH_KEY_MENU);         /* into the list, at "tbo" */
    CHECK(u.m.state == SH_MENU_LIST && u.m.pos == SH_SET_TANK_MAX);
    CHECK(shows(&u, S_t, S_b, S_o));
}

static void test_menu_list_order_and_names(void)
{
    ui_t u = ui_at(0);
    CHECK(shows(&u, S_t, S_b, S_o));                /* tbo */
    press(&u, SH_KEY_PLUS);
    CHECK(shows(&u, S_C | DOT, S_L, S_o));          /* C.Lo */
    press(&u, SH_KEY_PLUS);
    CHECK(shows(&u, S_d | DOT, S_o, S_n));          /* d.on */
    press(&u, SH_KEY_PLUS);
    CHECK(shows(&u, S_d | DOT, S_o, S_F));          /* d.oF */
    press(&u, SH_KEY_PLUS);
    CHECK(shows(&u, S_t, S_S, S_t));                /* tSt */
    press(&u, SH_KEY_PLUS);
    CHECK(shows(&u, S_E, S_n, S_d));                /* End */
    CHECK(u.m.pos == SH_MENU_POS_END);
    press(&u, SH_KEY_PLUS);                         /* round: after End comes tbo */
    CHECK(shows(&u, S_t, S_b, S_o));
    press(&u, SH_KEY_MINUS);                        /* and back */
    CHECK(shows(&u, S_E, S_n, S_d));
    press(&u, SH_KEY_MINUS);
    CHECK(shows(&u, S_t, S_S, S_t));
    wait(&u, 700);                                  /* names do not blink */
    CHECK(shows(&u, S_t, S_S, S_t));
}

static void test_menu_list_no_auto_repeat(void)
{
    ui_t u = ui_at(1);
    press(&u, SH_KEY_PLUS_REPEAT);
    press(&u, SH_KEY_MINUS_REPEAT);
    CHECK(u.m.pos == 1);
}

static void test_menu_end_leaves(void)
{
    ui_t u = ui_at(SH_MENU_POS_END);
    press(&u, SH_KEY_MENU);
    CHECK(u.m.state == SH_MENU_OFF);
    CHECK(u.saves == 0);
    press(&u, SH_KEY_MENU);         /* the next time the list starts at "tbo" again */
    CHECK(u.m.state == SH_MENU_LIST && u.m.pos == 0);
}

static void test_menu_value_shown_right_aligned(void)
{
    ui_t u = ui_edit(SH_SET_TANK_MAX);
    CHECK(shows(&u, 0, D[7], D[0]));                /* " 70" */
    u = ui_edit(SH_SET_DIFF_OFF);
    CHECK(shows(&u, 0, 0, D[4]));                   /* "  4" */
    u = ui_edit(SH_SET_TEST_S);
    CHECK(shows(&u, D[3], D[0], D[0]));             /* "300" */

    uint8_t seg[SH_MENU_DIGITS];
    sh_menu_number(0, seg);
    CHECK(seg[0] == 0 && seg[1] == 0 && seg[2] == D[0]);
    sh_menu_number(105, seg);
    CHECK(seg[0] == D[1] && seg[1] == D[0] && seg[2] == D[5]);
    sh_menu_number(900, seg);
    CHECK(seg[0] == D[9] && seg[1] == D[0] && seg[2] == D[0]);
}

static void test_menu_value_blinks(void)
{
    ui_t u = ui_edit(SH_SET_TANK_MAX);
    CHECK(shows(&u, 0, D[7], D[0]));                /* on for 0.5 s */
    wait(&u, 499);
    CHECK(shows(&u, 0, D[7], D[0]));
    wait(&u, 1);                                    /* off for 0.5 s */
    CHECK(shows(&u, 0, 0, 0));
    wait(&u, 499);
    CHECK(shows(&u, 0, 0, 0));
    wait(&u, 1);
    CHECK(shows(&u, 0, D[7], D[0]));
    wait(&u, 700);                                  /* in the dark half */
    CHECK(shows(&u, 0, 0, 0));
    key(&u, SH_KEY_PLUS);                           /* a changed value is shown at once */
    CHECK(shows(&u, 0, D[7], D[1]));
    wait(&u, 499);
    CHECK(shows(&u, 0, D[7], D[1]));
    wait(&u, 1);
    CHECK(shows(&u, 0, 0, 0));
}

static void test_menu_value_step(void)
{
    ui_t u = ui_edit(SH_SET_TANK_MAX);
    press(&u, SH_KEY_PLUS);
    CHECK(u.m.value == 71);
    press(&u, SH_KEY_MINUS);
    press(&u, SH_KEY_MINUS);
    CHECK(u.m.value == 69);
    CHECK(u.set.v[SH_SET_TANK_MAX] == 70);          /* nothing stored before the confirmation */

    u = ui_edit(SH_SET_TEST_S);                     /* step 30 s */
    press(&u, SH_KEY_PLUS);
    CHECK(u.m.value == 330);
    press(&u, SH_KEY_MINUS);
    press(&u, SH_KEY_MINUS);
    CHECK(u.m.value == 270);
}

static void test_menu_value_auto_repeat(void)
{
    ui_t u = ui_edit(SH_SET_COL_MIN);
    for (int i = 0; i < 5; i++) {                   /* held: a step every 200 ms */
        wait(&u, 200);
        key(&u, SH_KEY_PLUS_REPEAT);
    }
    CHECK(u.m.value == 30);
    for (int i = 0; i < 7; i++) {
        wait(&u, 200);
        key(&u, SH_KEY_MINUS_REPEAT);
    }
    CHECK(u.m.value == 23);
}

/* The value of `id` goes from its range minimum to its maximum and stops there: no wrap. */
static void check_range(sh_setting_id_t id, const sh_settings_t *set, int16_t min, int16_t max)
{
    ui_t u = ui_new();
    u.set = *set;
    press(&u, SH_KEY_MENU);
    for (int i = 0; i < (int)id; i++) {
        press(&u, SH_KEY_PLUS);
    }
    press(&u, SH_KEY_MENU);
    for (int i = 0; i < 1000; i++) {
        press(&u, SH_KEY_MINUS);
    }
    CHECK(u.m.value == min);
    press(&u, SH_KEY_MINUS_REPEAT);
    CHECK(u.m.value == min);
    for (int i = 0; i < 1000; i++) {
        press(&u, SH_KEY_PLUS);
    }
    CHECK(u.m.value == max);
    press(&u, SH_KEY_PLUS_REPEAT);
    CHECK(u.m.value == max);
}

static void test_menu_value_ranges(void)
{
    check_range(SH_SET_TANK_MAX, &DEF, 30, 85);
    check_range(SH_SET_COL_MIN, &DEF, 10, 80);
    check_range(SH_SET_DIFF_ON, &DEF, 5, 20);       /* off is 4: on >= 5 */
    check_range(SH_SET_DIFF_OFF, &DEF, 1, 5);       /* on is 6: off <= 5 */
    check_range(SH_SET_TEST_S, &DEF, 60, 900);

    const sh_settings_t wide = { .v = { 70, 25, 20, 1, 300 } };
    check_range(SH_SET_DIFF_ON, &wide, 3, 20);
    check_range(SH_SET_DIFF_OFF, &wide, 1, 15);

    const sh_settings_t odd = { .v = { 70, 25, 6, 4, 100 } };   /* a default not on the 30 s grid */
    check_range(SH_SET_TEST_S, &odd, 60, 900);
}

static void test_menu_confirm_stores_changed_value(void)
{
    ui_t u = ui_edit(SH_SET_TANK_MAX);
    press(&u, SH_KEY_MINUS);
    press(&u, SH_KEY_MINUS);
    press(&u, SH_KEY_MENU);
    CHECK(u.saves == 1 && u.set.v[SH_SET_TANK_MAX] == 68);
    CHECK(u.m.state == SH_MENU_SAVED);
    CHECK(shows(&u, 0, D[6], D[8]));
    wait(&u, 500);                                  /* steady: no dark half */
    CHECK(shows(&u, 0, D[6], D[8]));
    wait(&u, 499);                                  /* 999 ms */
    CHECK(u.m.state == SH_MENU_SAVED);
    CHECK(shows(&u, 0, D[6], D[8]));
    wait(&u, 1);                                    /* 1 s: the list, at the same position */
    CHECK(u.m.state == SH_MENU_LIST && u.m.pos == SH_SET_TANK_MAX);
    CHECK(shows(&u, S_t, S_b, S_o));
}

static void test_menu_confirm_same_value_not_stored(void)
{
    ui_t u = ui_edit(SH_SET_DIFF_ON);
    press(&u, SH_KEY_MENU);                         /* untouched */
    CHECK(u.saves == 0);
    CHECK(u.m.state == SH_MENU_SAVED);              /* shown steady all the same */
    CHECK(shows(&u, 0, 0, D[6]));
    wait(&u, 1000);
    CHECK(u.m.state == SH_MENU_LIST && u.m.pos == SH_SET_DIFF_ON);

    u = ui_edit(SH_SET_DIFF_ON);
    press(&u, SH_KEY_PLUS);
    press(&u, SH_KEY_MINUS);                        /* changed and changed back */
    press(&u, SH_KEY_MENU);
    CHECK(u.saves == 0);
}

static void test_menu_keys_wait_during_the_steady_second(void)
{
    ui_t u = ui_edit(SH_SET_TANK_MAX);
    press(&u, SH_KEY_PLUS);
    press(&u, SH_KEY_MENU);
    press(&u, SH_KEY_PLUS);
    press(&u, SH_KEY_MENU);
    CHECK(u.m.state == SH_MENU_SAVED && u.m.value == 71 && u.saves == 1);
    wait(&u, 800);
    CHECK(u.m.state == SH_MENU_LIST && u.m.pos == SH_SET_TANK_MAX);
}

static void test_menu_differences_follow_each_other(void)
{
    ui_t u = ui_edit(SH_SET_DIFF_OFF);              /* on 6: off up to 5 */
    press(&u, SH_KEY_PLUS);
    press(&u, SH_KEY_PLUS);
    CHECK(u.m.value == 5);
    press(&u, SH_KEY_MENU);
    CHECK(u.set.v[SH_SET_DIFF_OFF] == 5);
    wait(&u, 1000);
    press(&u, SH_KEY_MINUS);                        /* to d.on: now it cannot go below 6 */
    press(&u, SH_KEY_MENU);
    CHECK(u.m.state == SH_MENU_EDIT && u.m.pos == SH_SET_DIFF_ON);
    press(&u, SH_KEY_MINUS);
    CHECK(u.m.value == 6);
}

static void test_menu_timeout_in_the_list(void)
{
    ui_t u = ui_at(2);
    wait(&u, 9999);
    CHECK(u.m.state == SH_MENU_LIST);
    wait(&u, 1);                                    /* 10 s without a key */
    CHECK(u.m.state == SH_MENU_OFF);

    u = ui_at(2);
    wait(&u, 9999);
    key(&u, SH_KEY_PLUS);                           /* every key starts the 10 s again */
    wait(&u, 9999);
    CHECK(u.m.state == SH_MENU_LIST && u.m.pos == 3);
    wait(&u, 1);
    CHECK(u.m.state == SH_MENU_OFF);
}

static void test_menu_timeout_on_a_value_drops_the_change(void)
{
    ui_t u = ui_edit(SH_SET_TANK_MAX);
    press(&u, SH_KEY_PLUS);
    wait(&u, 9999);
    CHECK(u.m.state == SH_MENU_EDIT);
    wait(&u, 1);
    CHECK(u.m.state == SH_MENU_OFF);
    CHECK(u.saves == 0 && u.set.v[SH_SET_TANK_MAX] == 70);

    wait(&u, 100);                                  /* opened again: the old value */
    key(&u, SH_KEY_MENU);
    press(&u, SH_KEY_MENU);
    CHECK(u.m.state == SH_MENU_EDIT && u.m.value == 70);

    u = ui_edit(SH_SET_TANK_MAX);                   /* auto-repeat counts as keys */
    for (int i = 0; i < 60; i++) {
        wait(&u, 200);
        key(&u, SH_KEY_MINUS_REPEAT);
    }
    CHECK(u.m.state == SH_MENU_EDIT && u.m.value == 30);
}

static void test_menu_timeout_counts_from_the_confirmation(void)
{
    ui_t u = ui_edit(SH_SET_TANK_MAX);
    press(&u, SH_KEY_MENU);
    wait(&u, 9999);                                 /* 1 s steady + the list */
    CHECK(u.m.state == SH_MENU_LIST);
    wait(&u, 1);
    CHECK(u.m.state == SH_MENU_OFF);
}

static void test_menu_key_after_the_timeout(void)
{
    ui_t u = ui_edit(SH_SET_TANK_MAX);
    press(&u, SH_KEY_PLUS);
    u.now_ms += 10000;                              /* no tick in between */
    key(&u, SH_KEY_MENU);                           /* a key on the normal screen: opens the list */
    CHECK(u.saves == 0);
    CHECK(u.m.state == SH_MENU_LIST && u.m.pos == 0);
}

static void test_test_display(void)
{
    uint8_t seg[SH_MENU_DIGITS];
    sh_menu_test_label(seg);
    CHECK(seg[0] == S_t && seg[1] == S_S && seg[2] == S_t);

    CHECK(sh_menu_test_shows_label(0));             /* "tSt" for 3 s */
    CHECK(sh_menu_test_shows_label(2999));
    CHECK(!sh_menu_test_shows_label(3000));         /* the time left */
    CHECK(!sh_menu_test_shows_label(8999));
    CHECK(sh_menu_test_shows_label(9000));          /* "tSt" for 1 s... */
    CHECK(sh_menu_test_shows_label(9999));
    CHECK(!sh_menu_test_shows_label(10000));
    CHECK(!sh_menu_test_shows_label(15999));
    CHECK(sh_menu_test_shows_label(16000));         /* ...every 7 s */
    CHECK(sh_menu_test_shows_label(16999));
    CHECK(!sh_menu_test_shows_label(17000));
}

int main(void)
{
    test_settings_limits();
    test_settings_nothing_stored();
    test_settings_stored_values_used();
    test_settings_missing_value();
    test_settings_range_limits();
    test_settings_difference_pair();
    test_settings_range_narrowed_by_the_other_difference();

    test_menu_normal_screen();
    test_menu_list_order_and_names();
    test_menu_list_no_auto_repeat();
    test_menu_end_leaves();
    test_menu_value_shown_right_aligned();
    test_menu_value_blinks();
    test_menu_value_step();
    test_menu_value_auto_repeat();
    test_menu_value_ranges();
    test_menu_confirm_stores_changed_value();
    test_menu_confirm_same_value_not_stored();
    test_menu_keys_wait_during_the_steady_second();
    test_menu_differences_follow_each_other();
    test_menu_timeout_in_the_list();
    test_menu_timeout_on_a_value_drops_the_change();
    test_menu_timeout_counts_from_the_confirmation();
    test_menu_key_after_the_timeout();
    test_test_display();
    return test_result("settings and menu");
}
