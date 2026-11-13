/* Host test of group F (doc/tasks/step14-homekit.md 3, 5.2): HomeKit — the characteristics and their
 * limits (main/sh_hap_rules.c), the notifications, the writes of the controllers, when HAP starts and
 * which web server serves, switching HAP off while paired, the pairing window; the keypad lock and
 * the menu HAP / LoC (main/sh_menu.c); the display units °C / °F (main/sh_units.c); breathing and
 * Identify (main/sh_ind.c); the web interface (main/sh_web_api.c). Times in ms. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_hap_rules.h"
#include "sh_units.h"
#include "sh_menu.h"
#include "sh_ind.h"
#include "sh_web_api.h"

/* ---- the characteristics ---------------------------------------------------------------------- */

static sh_hap_in_t normal(void)
{
    const sh_hap_in_t in = { .t_tank = 418, .t_col = 523, .tmax = 70 };
    return in;
}

static sh_hap_view_t view_of(const sh_hap_in_t *in, const sh_hap_view_t *prev)
{
    sh_hap_view_t v;
    sh_hap_view(in, prev, &v);
    return v;
}

/* Active <-> vacation, both ways */
static void test_active(void)
{
    sh_hap_in_t in = normal();
    CHECK_EQ(view_of(&in, NULL).active, 1);
    in.vacation = true;                                     /* vacation from the menu / the web */
    CHECK_EQ(view_of(&in, NULL).active, 0);

    bool vac = false;
    CHECK(sh_hap_write_active(0, &vac));                    /* Off in Apple Home = vacation */
    CHECK(vac);
    CHECK(sh_hap_write_active(1, &vac));
    CHECK(!vac);
    CHECK(!sh_hap_write_active(2, &vac));                   /* invalid: nothing applied */
    CHECK(!vac);
}

/* Current State: Idle / Heating / Cooling; Target only Heat */
static void test_state(void)
{
    sh_hap_in_t in = normal();
    CHECK_EQ(view_of(&in, NULL).state, SH_HAP_STATE_IDLE);  /* the pump stands */
    in.pump_on = true;
    CHECK_EQ(view_of(&in, NULL).state, SH_HAP_STATE_HEATING);
    in.cooling = true;                                      /* night cooling */
    CHECK_EQ(view_of(&in, NULL).state, SH_HAP_STATE_COOLING);
    in.vacation = true;                                     /* the cooling of the vacation too */
    CHECK_EQ(view_of(&in, NULL).state, SH_HAP_STATE_COOLING);
    in.pump_on = false;                                     /* a stale cooling flag without the pump */
    CHECK_EQ(view_of(&in, NULL).state, SH_HAP_STATE_IDLE);

    CHECK(sh_hap_write_target(SH_HAP_TARGET_HEAT));
    CHECK(!sh_hap_write_target(0));                         /* Auto */
    CHECK(!sh_hap_write_target(2));                         /* Cool: the cooling tower, later */
}

/* Heating Threshold = Tmax, 30 ... 85 °C, whole degrees */
static void test_threshold(void)
{
    sh_hap_in_t in = normal();
    CHECK_EQ(view_of(&in, NULL).threshold, 70);
    in.tmax = 85;
    CHECK_EQ(view_of(&in, NULL).threshold, 85);
    in.tmax = 30;
    CHECK_EQ(view_of(&in, NULL).threshold, 30);

    int16_t t = 0;
    CHECK(sh_hap_write_threshold(55.0f, &t));
    CHECK_EQ(t, 55);
    CHECK(sh_hap_write_threshold(55.4f, &t));
    CHECK_EQ(t, 55);
    CHECK(sh_hap_write_threshold(55.5f, &t));
    CHECK_EQ(t, 56);
    CHECK(sh_hap_write_threshold(30.0f, &t));
    CHECK_EQ(t, 30);
    CHECK(sh_hap_write_threshold(85.0f, &t));
    CHECK_EQ(t, 85);
    t = 0;
    CHECK(!sh_hap_write_threshold(29.0f, &t));              /* out of the range: nothing applied */
    CHECK(!sh_hap_write_threshold(86.0f, &t));
    CHECK(!sh_hap_write_threshold(25.0f, &t));              /* the default range of HAP is 10 ... 25 */
    CHECK_EQ(t, 0);
}

/* Temperatures: the limits; a faulty sensor (or none yet) keeps the last value */
static void test_temperatures(void)
{
    sh_hap_in_t in = normal();
    sh_hap_view_t v = view_of(&in, NULL);
    CHECK_EQ(v.tank, 418);
    CHECK_EQ(v.col, 523);
    in.t_col = 1850;                                        /* the collector up to 200 °C */
    in.t_tank = 990;
    v = view_of(&in, &v);
    CHECK_EQ(v.col, 1850);
    CHECK_EQ(v.tank, 990);
    in.t_col = 2300;
    in.t_tank = 1020;
    v = view_of(&in, &v);
    CHECK_EQ(v.col, 2000);                                  /* the limits of the characteristics */
    CHECK_EQ(v.tank, 1000);
    in.t_col = -523;
    in.t_tank = -15;
    v = view_of(&in, &v);
    CHECK_EQ(v.col, -400);
    CHECK_EQ(v.tank, 0);
    in.t_col = -123;
    v = view_of(&in, &v);
    CHECK_EQ(v.col, -123);

    in = normal();
    v = view_of(&in, NULL);
    in.col_fault = true;                                    /* faulty: the last value stays */
    in.t_col = 999;
    in.tank_fault = true;
    in.t_tank = 777;
    v = view_of(&in, &v);
    CHECK_EQ(v.col, 523);
    CHECK_EQ(v.tank, 418);
    in.col_fault = in.tank_fault = false;
    in.t_col = in.t_tank = SH_TEMP_INVALID;                 /* no value yet */
    v = view_of(&in, &v);
    CHECK_EQ(v.col, 523);
    CHECK_EQ(v.tank, 418);
    v = view_of(&in, NULL);                                 /* at the creation: 0 */
    CHECK_EQ(v.col, 0);
    CHECK_EQ(v.tank, 0);
}

/* Status Fault: Collector E1, E5; Tank E2, E4; the rest nothing */
static void test_status_fault(void)
{
    static const struct {
        sh_alarm_t a;
        uint8_t col, tank;
    } cases[] = {
        { SH_ALARM_E1, 1, 0 }, { SH_ALARM_E5, 1, 0 }, { SH_ALARM_E2, 0, 1 }, { SH_ALARM_E4, 0, 1 },
        { SH_ALARM_E6, 0, 0 }, { SH_ALARM_E8, 0, 0 }, { SH_ALARM_HOT, 0, 0 }, { SH_ALARM_FRO, 0, 0 },
    };
    sh_hap_in_t in = normal();
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        in.alarms = (uint8_t)SH_ALARM_BIT(cases[i].a);
        const sh_hap_view_t v = view_of(&in, NULL);
        CHECK_EQ(v.col_fault, cases[i].col);
        CHECK_EQ(v.tank_fault, cases[i].tank);
    }
    in.alarms = SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_E4);
    const sh_hap_view_t v = view_of(&in, NULL);
    CHECK_EQ(v.col_fault, 1);
    CHECK_EQ(v.tank_fault, 1);
}

/* Units and the lock as they are; writes 0 / 1 only */
static void test_units_lock(void)
{
    sh_hap_in_t in = normal();
    CHECK_EQ(view_of(&in, NULL).units, 0);
    CHECK_EQ(view_of(&in, NULL).lock, 0);
    in.fahrenheit = true;
    in.locked = true;
    CHECK_EQ(view_of(&in, NULL).units, 1);
    CHECK_EQ(view_of(&in, NULL).lock, 1);
    bool f = false;
    CHECK(sh_hap_write_flag(1, &f));
    CHECK(f);
    CHECK(sh_hap_write_flag(0, &f));
    CHECK(!f);
    CHECK(!sh_hap_write_flag(2, &f));
}

/* Notify: when and when not */
static void test_notify(void)
{
    sh_hap_in_t in = normal();
    const sh_hap_view_t a = view_of(&in, NULL);
    sh_hap_view_t b = a;
    CHECK_EQ(sh_hap_notify(&a, &b), 0);

    /* the temperatures never (the controllers read them) */
    in.t_tank = 650;
    in.t_col = 1200;
    b = view_of(&in, &a);
    CHECK(b.tank != a.tank && b.col != a.col);
    CHECK_EQ(sh_hap_notify(&a, &b), 0);

    in = normal();
    in.vacation = true;
    CHECK_EQ(sh_hap_notify(&a, &(sh_hap_view_t){ 0 }) & SH_HAP_N_ACTIVE, SH_HAP_N_ACTIVE);
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&a, &b), SH_HAP_N_ACTIVE);
    in = normal();
    in.pump_on = true;
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&a, &b), SH_HAP_N_STATE);
    in = normal();
    in.tmax = 75;
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&a, &b), SH_HAP_N_THRESHOLD);
    in = normal();
    in.locked = true;
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&a, &b), SH_HAP_N_LOCK);
    in = normal();
    in.fahrenheit = true;
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&a, &b), SH_HAP_N_UNITS);
    in = normal();
    in.alarms = SH_ALARM_BIT(SH_ALARM_E5);
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&a, &b), SH_HAP_N_COL_FAULT);
    in.alarms = SH_ALARM_BIT(SH_ALARM_E2);
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&a, &b), SH_HAP_N_TANK_FAULT);
    in.alarms = SH_ALARM_BIT(SH_ALARM_E6) | SH_ALARM_BIT(SH_ALARM_HOT);    /* no Status Fault */
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&a, &b), 0);
    /* back again: notified again */
    CHECK_EQ(sh_hap_notify(&b, &a), 0);
    in = normal();
    in.vacation = in.pump_on = in.cooling = in.locked = true;
    b = view_of(&in, &a);
    CHECK_EQ(sh_hap_notify(&b, &a), SH_HAP_N_ACTIVE | SH_HAP_N_STATE | SH_HAP_N_LOCK);
}

/* When HAP starts and which web server serves */
static void test_start(void)
{
    CHECK(sh_hap_should_start(true, false, true));           /* the setting, the router */
    CHECK(!sh_hap_should_start(false, false, true));         /* off */
    CHECK(!sh_hap_should_start(true, false, false));         /* portal, AP, the router does not answer */
    CHECK(!sh_hap_should_start(true, true, true));           /* once: never stopped, never again */

    /* without HAP: the own server on the router and on the own access point (as before) */
    CHECK(sh_hap_own_web(false, false, true, false));
    CHECK(sh_hap_own_web(false, false, false, true));
    CHECK(!sh_hap_own_web(false, false, false, false));
    /* with HAP: on the router the server of HAP; the own one only in AP before HAP started */
    CHECK(!sh_hap_own_web(true, false, true, false));
    CHECK(!sh_hap_own_web(true, true, true, false));
    CHECK(sh_hap_own_web(true, false, false, true));
    CHECK(!sh_hap_own_web(true, true, false, true));
    CHECK(!sh_hap_own_web(true, false, false, false));
}

/* Switching HAP: a change restarts; off while paired is refused */
static void test_switch(void)
{
    CHECK_EQ(sh_hap_switch(true, true, false), SH_HAP_SW_NONE);
    CHECK_EQ(sh_hap_switch(true, true, true), SH_HAP_SW_NONE);
    CHECK_EQ(sh_hap_switch(true, false, false), SH_HAP_SW_RESTART);
    CHECK_EQ(sh_hap_switch(true, false, true), SH_HAP_SW_REFUSED);
    CHECK_EQ(sh_hap_switch(false, true, false), SH_HAP_SW_RESTART);
    CHECK_EQ(sh_hap_switch(false, false, false), SH_HAP_SW_NONE);
}

/* The pairing window (the LED breathes only while it is open) */
static void test_window(void)
{
    CHECK(sh_hap_window(false, SH_HAP_W_STARTED, 0));        /* not paired: open at the start */
    CHECK(!sh_hap_window(false, SH_HAP_W_STARTED, 1));       /* paired: closed */
    CHECK(!sh_hap_window(true, SH_HAP_W_PAIRED, 1));
    CHECK(!sh_hap_window(true, SH_HAP_W_TIMEOUT, 0));        /* 10 min over: HAP keeps running */
    CHECK(sh_hap_window(false, SH_HAP_W_UNPAIRED, 0));       /* the last controller removed */
    CHECK(!sh_hap_window(false, SH_HAP_W_UNPAIRED, 1));      /* another one stays */

    sh_ind_state_t st = { 0 };
    const sh_ind_params_t p = { .cold = 200, .hot = 900 };
    sh_ind_in_t in = { .t_col = 500, .wifi_connected = true, .pair_window = true };
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_BREATHE);
    CHECK_EQ(sh_ind_update(&st, &p, &in).color, SH_COLOR_BLUE);     /* the colour of the process */
    in.pump_on = true;
    CHECK_EQ(sh_ind_update(&st, &p, &in).color, SH_COLOR_GREEN);
    in.pair_window = false;                                  /* otherwise the connection */
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_STEADY);
    in.pair_window = true;
    in.wifi_connected = false;
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_HEARTBEAT);
    in.wifi_connected = true;
    in.alarms = SH_ALARM_BIT(SH_ALARM_E4);                   /* alarms and OTA first */
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_FAST);
    in.alarms = 0;
    in.ota = true;
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_BLINK);

    /* breathing: dark 400 ms, then a squared ramp in and out over 5 s */
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, 0), 0);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, 399), 0);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, 400 + 2500), SH_IND_LEVEL_MAX);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, 400 + 1250), SH_IND_LEVEL_MAX / 4);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, 400 + 3750), SH_IND_LEVEL_MAX / 4);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, 5400), 0);         /* the next breath */
    CHECK(sh_ind_level(SH_IND_BREATHE, 400 + 1000) < sh_ind_level(SH_IND_BREATHE, 400 + 2000));
    CHECK(sh_ind_lit(SH_IND_BREATHE, 2900));
    CHECK(!sh_ind_lit(SH_IND_BREATHE, 100));
    CHECK_EQ(sh_ind_level(SH_IND_STEADY, 1234), SH_IND_LEVEL_MAX);
    CHECK_EQ(sh_ind_level(SH_IND_HEARTBEAT, 500), 0);
}

/* Identify: ledIdentify() of outputwrite — 3 x (2 blinks @ 200 ms), 300 ms gap */
static void test_identify(void)
{
    bool lit;
    CHECK_EQ(SH_IND_IDENT_MS, 3000);
    const uint32_t dark[] = { 0, 199, 400, 599, 1100, 1500, 2200, 2600, 2799 };
    const uint32_t on[] = { 200, 399, 600, 799, 800, 1099, 1300, 1700, 2000, 2999 };
    for (unsigned i = 0; i < sizeof(dark) / sizeof(dark[0]); i++) {
        CHECK(sh_ind_identify(dark[i], &lit));
        CHECK(!lit);
    }
    for (unsigned i = 0; i < sizeof(on) / sizeof(on[0]); i++) {
        CHECK(sh_ind_identify(on[i], &lit));
        CHECK(lit);
    }
    CHECK(!sh_ind_identify(3000, &lit));                     /* over: back to the indication */
    unsigned edges = 0;
    bool was = true;
    for (uint32_t ms = 0; ms < SH_IND_IDENT_MS; ms += 10) {
        sh_ind_identify(ms, &lit);
        edges += !lit && was;
        was = lit;
    }
    CHECK_EQ(edges, 6);                                      /* 3 x 2 blinks */
}

/* ---- the display units -------------------------------------------------------------------------- */

static void test_units(void)
{
    CHECK_EQ(sh_units_temp10(232, SH_UNITS_C), 232);         /* °C: as it is */
    CHECK_EQ(sh_units_temp10(232, SH_UNITS_F), 738);         /* 23.2 °C = 73.76 -> 73.8 °F */
    CHECK_EQ(sh_units_temp10(0, SH_UNITS_F), 320);
    CHECK_EQ(sh_units_temp10(1000, SH_UNITS_F), 2120);
    CHECK_EQ(sh_units_temp10(1500, SH_UNITS_F), 3020);       /* 150 °C: "302" */
    CHECK_EQ(sh_units_temp10(2000, SH_UNITS_F), 3920);
    CHECK_EQ(sh_units_temp10(-99, SH_UNITS_F), 142);         /* -9.9 °C = 14.18 °F */
    CHECK_EQ(sh_units_temp10(-400, SH_UNITS_F), -400);       /* -40 is -40 */
    CHECK_EQ(sh_units_temp10(-551, SH_UNITS_F), -672);       /* -55.1 = -67.18 */
    CHECK_EQ(sh_units_temp10(1, SH_UNITS_F), 322);           /* 0.18 rounds up */
    CHECK_EQ(sh_units_temp(70, SH_UNITS_F), 158);
    CHECK_EQ(sh_units_temp(30, SH_UNITS_F), 86);
    CHECK_EQ(sh_units_temp(85, SH_UNITS_F), 185);
    CHECK_EQ(sh_units_temp(31, SH_UNITS_F), 88);             /* 87.8 */
    CHECK_EQ(sh_units_temp(-18, SH_UNITS_F), 0);             /* -0.4 */
    CHECK_EQ(sh_units_temp(70, SH_UNITS_C), 70);
    CHECK_EQ(sh_units_diff(6, SH_UNITS_F), 11);              /* 10.8: a difference, no +32 */
    CHECK_EQ(sh_units_diff(4, SH_UNITS_F), 7);
    CHECK_EQ(sh_units_diff(1, SH_UNITS_F), 2);
    CHECK_EQ(sh_units_diff(20, SH_UNITS_F), 36);
    CHECK_EQ(sh_units_diff(6, SH_UNITS_C), 6);

    /* the menu: tbo / C.Lo as temperatures, d.on / d.oF as differences, the rest as it is */
    CHECK_EQ(sh_menu_value_shown(SH_MENU_TBO, 70, true), 158);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_CLO, 25, true), 77);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_DON, 6, true), 11);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_DOF, 4, true), 7);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_TST, 300, true), 300);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_TBO, 70, false), 70);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_DON, 6, false), 6);
}

/* ---- the menu: HAP, LoC, the keypad lock ------------------------------------------------------ */

static const sh_settings_t S = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };

typedef struct {
    sh_menu_t m;
    sh_menu_ctx_t ctx;
    sh_settings_t s;
    uint32_t now;
} ui_t;

static ui_t ui(void)
{
    ui_t u = { .s = S, .now = 1000, .ctx = { .hap_on = true } };
    sh_menu_sync_lock(&u.m, false, u.now);
    return u;
}

static sh_menu_act_t key(ui_t *u, sh_key_t k, uint32_t dt)
{
    u->now += dt;
    return sh_menu_key(&u->m, k, u->now, &u->ctx, &u->s);
}

static sh_menu_view_t view(ui_t *u, uint32_t dt)
{
    u->now += dt;
    sh_menu_sync_lock(&u->m, u->ctx.locked, u->now);
    sh_menu_update(&u->m, u->now);
    sh_menu_view_t v;
    sh_menu_view(&u->m, u->now, &v);
    return v;
}

/* the list at `pos`, from the start */
static void go(ui_t *u, uint8_t pos)
{
    key(u, SH_KEY_MENU, 100);
    while (u->m.pos != pos) {
        key(u, SH_KEY_PLUS, 100);
    }
}

/* "+" and "-" pressed together (`dt` apart), held `hold` ms, released; returns the action */
static sh_menu_act_t both(ui_t *u, uint32_t hold)
{
    sh_menu_button(&u->m, true, true, u->now);
    u->now += 50;
    sh_menu_button(&u->m, false, true, u->now);
    sh_menu_act_t act = SH_MENU_ACT_NONE;
    for (uint32_t t = 0; t < hold; t += 100) {
        u->now += 100;
        const sh_menu_act_t a = sh_menu_poll(&u->m, u->now, &u->ctx);
        if (a != SH_MENU_ACT_NONE) {
            CHECK_EQ(act, SH_MENU_ACT_NONE);                 /* once per hold */
            act = a;
        }
        if (t >= 1000 && t % 200 == 0) {                     /* the repetition of both */
            CHECK_EQ(key(u, SH_KEY_PLUS_REPEAT, 0), SH_MENU_ACT_NONE);
            CHECK_EQ(key(u, SH_KEY_MINUS_REPEAT, 0), SH_MENU_ACT_NONE);
        }
    }
    sh_menu_button(&u->m, true, false, u->now);
    sh_menu_button(&u->m, false, false, u->now);
    /* the taps the library reports on the release belong to the hold */
    CHECK_EQ(key(u, SH_KEY_PLUS, 0), SH_MENU_ACT_NONE);
    CHECK_EQ(key(u, SH_KEY_MINUS, 0), SH_MENU_ACT_NONE);
    return act;
}

static void test_menu_hap(void)
{
    /* SSn, HoL, tbo, C.Lo, d.on, d.oF, tSt, nEt, HAP, LoC, End */
    CHECK_EQ(SH_MENU_HAP, SH_MENU_NET + 1);
    CHECK_EQ(SH_MENU_LOC, SH_MENU_HAP + 1);
    CHECK_EQ(SH_MENU_END, SH_MENU_LOC + 1);

    ui_t u = ui();
    go(&u, SH_MENU_HAP);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    CHECK_EQ(view(&u, 0).value, 1);                          /* the current setting: on */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);   /* the same: nothing, no restart */
    CHECK_EQ(view(&u, 0).mode, SH_MENU_SAVED);
    CHECK_EQ(view(&u, 1000).mode, SH_MENU_LIST);

    key(&u, SH_KEY_MENU, 100);                               /* off, not paired: stored, restart */
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(view(&u, 0).value, 0);
    key(&u, SH_KEY_MINUS, 100);                              /* no wrap */
    CHECK_EQ(view(&u, 0).value, 0);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_HAP);
    CHECK_EQ(u.m.value, 0);

    u = ui();                                                /* paired: off is refused */
    u.ctx.hap_paired = true;
    go(&u, SH_MENU_HAP);
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    sh_menu_view_t v = view(&u, 0);
    CHECK_EQ(v.mode, SH_MENU_SAVED);                         /* "on" steady for 1 s */
    CHECK_EQ(v.value, 1);
    CHECK(v.value_visible);
    CHECK_EQ(view(&u, 1000).mode, SH_MENU_LIST);

    u = ui();                                                /* off -> on */
    u.ctx.hap_on = false;
    go(&u, SH_MENU_HAP);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(view(&u, 0).value, 0);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_HAP);
    CHECK_EQ(u.m.value, 1);
}

static void test_menu_loc(void)
{
    ui_t u = ui();
    go(&u, SH_MENU_LOC);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    CHECK_EQ(view(&u, 0).value, 0);                          /* always at a blinking "oFF" */
    CHECK(!view(&u, 500).value_visible);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);   /* oFF: nothing is locked */
    CHECK_EQ(view(&u, 0).mode, SH_MENU_SAVED);
    CHECK_EQ(view(&u, 1000).mode, SH_MENU_LIST);
    CHECK_EQ(u.m.pos, SH_MENU_LOC);

    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(view(&u, 0).value, 1);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_LOCK);   /* on + "menu": locked, the menu closes */
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    u.ctx.locked = true;                                     /* the store took it */
    sh_menu_view_t v = view(&u, 50);
    CHECK_EQ(v.notice, SH_MENU_NOTICE_LOC);                  /* "LoC" for 1 s */
    CHECK_EQ(v.mode, SH_MENU_NORMAL);
    CHECK_EQ(view(&u, 999).notice, SH_MENU_NOTICE_LOC);
    CHECK_EQ(view(&u, 1).notice, SH_MENU_NOTICE_NONE);

    /* opened by mistake: "menu" and out by End, nothing locked */
    u = ui();
    go(&u, SH_MENU_LOC);
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_MENU, 100);
    view(&u, 1000);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(u.m.pos, SH_MENU_END);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
}

/* Locked: every key does nothing and shows "LoC"; the holds of "menu" too */
static void test_locked_keys(void)
{
    ui_t u = ui();
    u.ctx.locked = true;
    view(&u, 0);                                             /* the lock came from elsewhere */
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    view(&u, 1000);

    const sh_key_t keys[] = { SH_KEY_MENU, SH_KEY_PLUS, SH_KEY_MINUS, SH_KEY_PLUS_REPEAT, SH_KEY_MINUS_REPEAT };
    const bool tests[] = { false, true };
    for (unsigned t = 0; t < 2; t++) {
        u.ctx.test_running = tests[t];
        u.ctx.air_running = !tests[t];
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            CHECK_EQ(key(&u, keys[i], 2000), SH_MENU_ACT_NONE);    /* no menu, test, stop, ±60 s */
            CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
            CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
        }
    }
    u.ctx.test_running = u.ctx.air_running = false;
    u.ctx.fup_offer = true;                                  /* nor the confirmation of FUp */
    CHECK_EQ(key(&u, SH_KEY_MENU, 2000), SH_MENU_ACT_NONE);
    u.ctx.fup_offer = false;

    view(&u, 2000);                                          /* the holds of "menu": refused */
    CHECK(!sh_menu_hold(&u.m, u.now, &u.ctx));
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    u.ctx.locked = false;
    view(&u, 0);
    view(&u, 2000);
    CHECK(sh_menu_hold(&u.m, u.now, &u.ctx));                /* unlocked: the resets work */
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_NONE);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_PLUS);   /* the test again */
}

/* "+" and "-" held together for 3 s */
static void test_combo(void)
{
    ui_t u = ui();
    CHECK_EQ(both(&u, 2900), SH_MENU_ACT_NONE);              /* not long enough */
    CHECK_EQ(both(&u, 3100), SH_MENU_ACT_LOCK);              /* the normal screen: locks, no test */
    CHECK_EQ(both(&u, 10000), SH_MENU_ACT_LOCK);             /* once per hold (checked in both()) */

    u.ctx.locked = true;                                     /* locked: unlocks, whatever is shown */
    view(&u, 0);
    CHECK_EQ(both(&u, 3100), SH_MENU_ACT_UNLOCK);
    u.ctx.test_running = true;
    CHECK_EQ(both(&u, 3100), SH_MENU_ACT_UNLOCK);
    u.ctx.test_running = false;
    u.ctx.air_running = true;
    CHECK_EQ(both(&u, 3100), SH_MENU_ACT_UNLOCK);
    u.ctx.air_running = false;
    u.ctx.locked = false;
    sh_menu_view_t v = view(&u, 0);
    CHECK_EQ(v.notice, SH_MENU_NOTICE_UNL);                  /* "UnL" for 1 s */
    CHECK_EQ(view(&u, 1000).notice, SH_MENU_NOTICE_NONE);

    /* unlocked: in the menu and during the test nothing — no lock, no value change */
    u = ui();
    go(&u, SH_MENU_TBO);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(both(&u, 3100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    CHECK_EQ(u.m.value, 70);                                 /* the repeats did not move it */
    u = ui();
    u.ctx.test_running = true;
    CHECK_EQ(both(&u, 3100), SH_MENU_ACT_NONE);
    u.ctx.test_running = false;
    u.ctx.air_running = true;
    CHECK_EQ(both(&u, 3100), SH_MENU_ACT_NONE);

    /* one button alone afterwards: a normal key again */
    u = ui();
    both(&u, 1000);
    sh_menu_button(&u.m, true, true, u.now);
    sh_menu_button(&u.m, true, false, u.now + 100);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_PLUS);
    /* a released button ends the hold: the other one alone does not lock */
    u = ui();
    sh_menu_button(&u.m, true, true, u.now);
    sh_menu_button(&u.m, false, true, u.now);
    sh_menu_button(&u.m, false, false, u.now + 1000);
    CHECK_EQ(sh_menu_poll(&u.m, u.now + 4000, &u.ctx), SH_MENU_ACT_NONE);
}

/* A lock from Apple Home / the web while the menu is open: "LoC", the normal screen, the value lost */
static void test_lock_elsewhere(void)
{
    ui_t u = ui();
    go(&u, SH_MENU_TBO);
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_PLUS, 100);
    u.ctx.locked = true;
    sh_menu_view_t v = view(&u, 100);
    CHECK_EQ(v.mode, SH_MENU_NORMAL);
    CHECK_EQ(v.notice, SH_MENU_NOTICE_LOC);
    u.ctx.locked = false;                                    /* unlocked elsewhere: "UnL" */
    v = view(&u, 2000);
    CHECK_EQ(v.notice, SH_MENU_NOTICE_UNL);
    CHECK_EQ(v.mode, SH_MENU_NORMAL);
    /* the test runs on underneath: no key, no action */
    u.ctx.test_running = true;
    u.ctx.locked = true;
    v = view(&u, 2000);
    CHECK_EQ(v.notice, SH_MENU_NOTICE_LOC);
    CHECK_EQ(view(&u, 1000).notice, SH_MENU_NOTICE_NONE);

    /* at boot the stored lock is only taken, no "LoC" */
    sh_menu_t m = { 0 };
    sh_menu_sync_lock(&m, true, 5000);
    sh_menu_view_t w;
    sh_menu_view(&m, 5000, &w);
    CHECK_EQ(w.notice, SH_MENU_NOTICE_NONE);
}

/* °F in the menu: the step stays 1 °C, the logic gets the same °C values */
static void test_menu_fahrenheit(void)
{
    /* tbo from 30 up: 86, 88, 90, 91, 93, 95, 97, 99, 100 °F */
    ui_t u = ui();
    u.s.v[SH_SET_TANK_MAX] = 30;
    go(&u, SH_MENU_TBO);
    key(&u, SH_KEY_MENU, 100);
    const int16_t f[] = { 86, 88, 90, 91, 93, 95, 97, 99, 100 };
    for (unsigned i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
        CHECK_EQ(sh_menu_value_shown(SH_MENU_TBO, view(&u, 0).value, true), f[i]);
        CHECK_EQ(view(&u, 0).value, 30 + (int)i);           /* kept in °C */
        key(&u, SH_KEY_PLUS, 100);
    }
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_SAVE);
    CHECK_EQ(u.m.value, 38);                                 /* stored in °C: shown 100 °F */

    /* d.on in °F: 1 K = 1.8 °F steps */
    u = ui();
    go(&u, SH_MENU_DON);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_DON, view(&u, 0).value, true), 11);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_DON, view(&u, 0).value, true), 13);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(sh_menu_value_shown(SH_MENU_DON, view(&u, 0).value, true), 14);

    /* the same keys give the same action and value with °C and °F: the menu does not know the units */
    for (int units = 0; units < 2; units++) {
        ui_t a = ui();
        go(&a, SH_MENU_CLO);
        key(&a, SH_KEY_MENU, 100);
        key(&a, SH_KEY_PLUS, 100);
        key(&a, SH_KEY_PLUS, 100);
        (void)sh_menu_value_shown(SH_MENU_CLO, a.m.value, units != 0);
        CHECK_EQ(key(&a, SH_KEY_MENU, 100), SH_MENU_ACT_SAVE);
        CHECK_EQ(a.m.value, 27);
        CHECK_EQ(a.m.id, SH_SET_COL_MIN);
    }
}

/* ---- the web interface -------------------------------------------------------------------------- */

#define CODE    "81084A"

static void test_web(void)
{
    int hap = 9, lock = 9;
    CHECK_EQ(sh_web_hk("lock=1&code=81084a", CODE, false, &hap, &lock), SH_WEB_OK);
    CHECK_EQ(hap, -1);
    CHECK_EQ(lock, 1);
    CHECK_EQ(sh_web_hk("lock=0&code=" CODE, CODE, true, &hap, &lock), SH_WEB_OK);
    CHECK_EQ(lock, 0);
    CHECK_EQ(sh_web_hk("hap=0&code=" CODE, CODE, false, &hap, &lock), SH_WEB_OK);
    CHECK_EQ(hap, 0);
    CHECK_EQ(lock, -1);
    CHECK_EQ(sh_web_hk("hap=1&code=" CODE, CODE, true, &hap, &lock), SH_WEB_OK);
    CHECK_EQ(hap, 1);
    hap = lock = 9;
    CHECK_EQ(sh_web_hk("hap=0&code=" CODE, CODE, true, &hap, &lock), SH_WEB_BAD);   /* paired */
    CHECK_EQ(hap, 9);
    CHECK_EQ(sh_web_hk("lock=1", CODE, false, &hap, &lock), SH_WEB_DENIED);         /* no code */
    CHECK_EQ(sh_web_hk("lock=1&code=123456", CODE, false, &hap, &lock), SH_WEB_DENIED);
    CHECK_EQ(sh_web_hk("lock=2&code=" CODE, CODE, false, &hap, &lock), SH_WEB_BAD);
    CHECK_EQ(sh_web_hk("units=1&code=" CODE, CODE, false, &hap, &lock), SH_WEB_BAD); /* only from HomeKit */
    CHECK_EQ(sh_web_hk("code=" CODE, CODE, false, &hap, &lock), SH_WEB_BAD);
    CHECK_EQ(sh_web_hk("lock=1&lock", CODE, false, &hap, &lock), SH_WEB_BAD);
    CHECK_EQ(hap, 9);

    sh_web_reset_t what = SH_WEB_RESET_WIFI;
    CHECK_EQ(sh_web_reset("what=pairings&code=" CODE, CODE, &what), SH_WEB_OK);
    CHECK_EQ(what, SH_WEB_RESET_PAIRINGS);
    CHECK_EQ(sh_web_reset("what=pairings", CODE, &what), SH_WEB_DENIED);

    static char json[SH_WEB_STATUS_MAX];
    sh_web_status_t s = {
        .t_col = 523, .t_tank = 418, .cfg = { .settings = S }, .name = "Solar-810840", .version = "0.1.0",
        .serial = "E8F60A810840", .ssid = "Home", .ip = "192.168.1.7", .host = "Solar-810840",
        .ap_ssid = "Solar-810840", .clock = { .tz = "UTC", .min = -1, .peak = -1 }, .cool_until = -1,
        .sunrise = -1, .sunset = -1, .e8_normal = INT16_MIN,
        .hk_on = true, .hk_run = true, .hk_paired = true, .locked = true, .fahrenheit = true,
    };
    CHECK(sh_web_status_json(json, sizeof(json), &s) > 0);
    CHECK(strstr(json, "\"hk\":{\"on\":1,\"run\":1,\"paired\":1,\"lock\":1,\"f\":1}") != NULL);
    CHECK(strstr(json, "\"homekit\",\"keylock\"") != NULL);  /* the elements are not grey */
    CHECK(strstr(json, "\"tank\":418,\"col\":523,") != NULL); /* the API stays in tenths of °C */
    CHECK(strstr(json, "\"tbo\":{\"v\":70,") != NULL);
    s.hk_on = s.hk_run = s.hk_paired = s.locked = s.fahrenheit = false;
    CHECK(sh_web_status_json(json, sizeof(json), &s) > 0);
    CHECK(strstr(json, "\"hk\":{\"on\":0,\"run\":0,\"paired\":0,\"lock\":0,\"f\":0}") != NULL);
}

int main(void)
{
    test_active();
    test_state();
    test_threshold();
    test_temperatures();
    test_status_fault();
    test_units_lock();
    test_notify();
    test_start();
    test_switch();
    test_window();
    test_identify();
    test_units();
    test_menu_hap();
    test_menu_loc();
    test_locked_keys();
    test_combo();
    test_lock_elsewhere();
    test_menu_fahrenheit();
    test_web();
    return test_summary("test_hap");
}
