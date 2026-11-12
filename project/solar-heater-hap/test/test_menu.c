/* Host test of the menu transitions (main/sh_menu.c; doc/tasks/step2-settings-menu-pump-sensors.md
 * 3.2) and of the pump test screen. Times in ms. */

#include "test_util.h"
#include "sh_menu.h"

static const sh_settings_t S = { .v = { 70, 25, 6, 4, 300 } };

typedef struct {
    sh_menu_t m;
    sh_settings_t s;
    uint32_t now;
    bool test;
} ui_t;

static ui_t ui(void)
{
    ui_t u = { .s = S, .now = 1000 };
    return u;
}

/* a key `dt` ms after the previous event */
static sh_menu_act_t key(ui_t *u, sh_key_t k, uint32_t dt)
{
    u->now += dt;
    return sh_menu_key(&u->m, k, u->now, u->test, &u->s);
}

static sh_menu_view_t view(ui_t *u, uint32_t dt)
{
    u->now += dt;
    sh_menu_update(&u->m, u->now);
    sh_menu_view_t v;
    sh_menu_view(&u->m, u->now, &v);
    return v;
}

static void test_normal_screen(void)
{
    ui_t u = ui();
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 0), SH_MENU_ACT_PLUS);        /* pump test */
    CHECK_EQ(key(&u, SH_KEY_MINUS, 0), SH_MENU_ACT_NONE);       /* nothing without a test */
    CHECK_EQ(key(&u, SH_KEY_PLUS_REPEAT, 0), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);

    u.test = true;
    CHECK_EQ(key(&u, SH_KEY_PLUS, 0), SH_MENU_ACT_PLUS);        /* +60 s */
    CHECK_EQ(key(&u, SH_KEY_MINUS, 0), SH_MENU_ACT_MINUS);
    CHECK_EQ(key(&u, SH_KEY_MENU, 0), SH_MENU_ACT_STOP);        /* stops the test, no menu */
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);

    u.test = false;
    CHECK_EQ(key(&u, SH_KEY_MENU, 0), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_LIST);
    CHECK_EQ(u.m.pos, SH_SET_TANK_MAX);                         /* at tbo */
}

static void test_list_order_and_wrap(void)
{
    ui_t u = ui();
    key(&u, SH_KEY_MENU, 0);
    const uint8_t order[] = { SH_SET_COL_MIN, SH_SET_DIFF_ON, SH_SET_DIFF_OFF, SH_SET_TEST_S,
                              SH_MENU_END, SH_SET_TANK_MAX };
    for (unsigned i = 0; i < sizeof(order); i++) {
        CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_NONE);
        CHECK_EQ(u.m.pos, order[i]);
    }
    key(&u, SH_KEY_MINUS, 100);                                 /* tbo -> End */
    CHECK_EQ(u.m.pos, SH_MENU_END);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(u.m.pos, SH_SET_TEST_S);
    key(&u, SH_KEY_PLUS_REPEAT, 100);                           /* no repetition in the list */
    CHECK_EQ(u.m.pos, SH_SET_TEST_S);
    CHECK_EQ(u.m.mode, SH_MENU_LIST);
    sh_menu_view_t v = view(&u, 0);
    CHECK_EQ(v.mode, SH_MENU_LIST);
    CHECK_EQ(v.pos, SH_SET_TEST_S);
}

static void test_end_returns(void)
{
    ui_t u = ui();
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
}

static void test_edit_and_save(void)
{
    ui_t u = ui();
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MENU, 100);                                  /* open tbo */
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    CHECK_EQ(u.m.value, 70);
    key(&u, SH_KEY_PLUS, 100);
    key(&u, SH_KEY_PLUS, 100);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(u.m.value, 71);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_SAVE);      /* confirmed, different */
    CHECK_EQ(u.m.pos, SH_SET_TANK_MAX);
    CHECK_EQ(u.m.value, 71);
    u.s.v[SH_SET_TANK_MAX] = 71;                                /* (stored by the caller) */

    sh_menu_view_t v = view(&u, 0);                             /* steady for 1 s */
    CHECK_EQ(v.mode, SH_MENU_SAVED);
    CHECK(v.value_visible);
    v = view(&u, 999);
    CHECK_EQ(v.mode, SH_MENU_SAVED);
    CHECK(v.value_visible);
    v = view(&u, 1);
    CHECK_EQ(v.mode, SH_MENU_LIST);                             /* the list at the same item */
    CHECK_EQ(v.pos, SH_SET_TANK_MAX);

    key(&u, SH_KEY_MENU, 100);                                  /* same value: no save */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_SAVED);
    key(&u, SH_KEY_PLUS, 100);                                  /* keys ignored for that 1 s */
    CHECK_EQ(u.m.mode, SH_MENU_SAVED);
    CHECK_EQ(u.m.value, 71);
}

static void test_edit_limits_and_repeat(void)
{
    ui_t u = ui();
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_PLUS, 100);
    key(&u, SH_KEY_PLUS, 100);                                  /* d.on */
    key(&u, SH_KEY_MENU, 100);
    for (int i = 0; i < 5; i++) {
        key(&u, SH_KEY_MINUS_REPEAT, 200);
    }
    CHECK_EQ(u.m.value, 5);                                     /* d.oF + 1, no wrap */
    for (int i = 0; i < 30; i++) {
        key(&u, SH_KEY_PLUS_REPEAT, 200);
    }
    CHECK_EQ(u.m.value, 20);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);                           /* the repetition keeps it open */

    u = ui();                                                   /* tSt: 30 s steps */
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MINUS, 100);
    key(&u, SH_KEY_MINUS, 100);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(u.m.pos, SH_SET_TEST_S);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(u.m.value, 330);
}

static void test_blink(void)
{
    ui_t u = ui();
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MENU, 100);
    sh_menu_view_t v = view(&u, 0);
    CHECK_EQ(v.mode, SH_MENU_EDIT);
    CHECK(v.value_visible);
    CHECK(view(&u, 499).value_visible);
    CHECK(!view(&u, 1).value_visible);                          /* 500 ms */
    CHECK(!view(&u, 499).value_visible);
    CHECK(view(&u, 1).value_visible);                           /* 1000 ms */
    view(&u, 600);
    CHECK(!view(&u, 0).value_visible);
    key(&u, SH_KEY_PLUS, 0);                                    /* a new value is shown at once */
    CHECK(view(&u, 0).value_visible);
}

static void test_timeout(void)
{
    ui_t u = ui();
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(view(&u, 9999).mode, SH_MENU_LIST);
    CHECK_EQ(view(&u, 1).mode, SH_MENU_NORMAL);                 /* 10 s without a key */

    u = ui();                                                   /* on a value: not saved */
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(view(&u, 9999).mode, SH_MENU_EDIT);
    CHECK_EQ(key(&u, SH_KEY_MENU, 1), SH_MENU_ACT_NONE);        /* too late: no confirmation */
    CHECK_EQ(u.m.mode, SH_MENU_LIST);                           /* this "menu" entered the list again */

    u = ui();                                                   /* each key restarts the 10 s */
    key(&u, SH_KEY_MENU, 0);
    for (int i = 0; i < 5; i++) {
        key(&u, SH_KEY_PLUS, 9000);
    }
    CHECK_EQ(view(&u, 0).mode, SH_MENU_LIST);

    u = ui();                                                   /* after a confirmation */
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(view(&u, 9999).mode, SH_MENU_LIST);
    CHECK_EQ(view(&u, 1).mode, SH_MENU_NORMAL);

    u = ui();                                                   /* the normal screen has none */
    CHECK_EQ(view(&u, 100000).mode, SH_MENU_NORMAL);
}

static void test_test_screen(void)
{
    for (uint32_t t = 0; t < 3; t++) {
        CHECK(sh_menu_test_label(t));                           /* tSt 3 s */
    }
    for (uint32_t t = 3; t < 9; t++) {
        CHECK(!sh_menu_test_label(t));                          /* seconds left */
    }
    CHECK(sh_menu_test_label(9));                               /* tSt 1 s every 7 s */
    for (uint32_t t = 10; t < 16; t++) {
        CHECK(!sh_menu_test_label(t));
    }
    CHECK(sh_menu_test_label(16));
    CHECK(sh_menu_test_label(23));
    CHECK(!sh_menu_test_label(24));
}

int main(void)
{
    test_normal_screen();
    test_list_order_and_wrap();
    test_end_returns();
    test_edit_and_save();
    test_edit_limits_and_repeat();
    test_blink();
    test_timeout();
    test_test_screen();
    return test_summary("test_menu");
}
