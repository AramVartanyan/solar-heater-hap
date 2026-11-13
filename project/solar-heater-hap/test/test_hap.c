/* Host test of group F (doc/tasks/step14-homekit.md): Apple HomeKit. What the accessory shows and
 * takes (main/sh_hap_rules.c): Active and the vacation, Tmax and its limits, the Current State, the
 * temperatures and their ranges, the display units, the keyboard lock, the Status Fault by the alarms,
 * when a notification goes out and when not, when HAP starts and which web server is used, the
 * setting "Apple HomeKit" and its refusal while paired, the pairing window. Then the menu position
 * HAP and "LoC" (main/sh_menu.c), the breathing LED and Identify (main/sh_ind.c) and what the web
 * interface gets and sends (main/sh_web_api.c, the page). Temperatures in tenths of °C, times in ms. */

#include <math.h>
#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_settings.h"
#include "sh_ind.h"
#include "sh_menu.h"
#include "sh_web_api.h"
#include "sh_hap_rules.h"

#define BIT(a)  SH_ALARM_BIT(a)
#define CODE    "810840"

static const sh_settings_t S = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };

/* Normal mode, Tmax 70 °C, the pump stands, nothing else. */
static sh_hap_in_t normal(void)
{
    const sh_hap_in_t in = { .tmax = 70, .reason = SH_PUMP_REASON_DIFF };
    return in;
}

static sh_hap_view_t view_of(const sh_hap_in_t *in)
{
    sh_hap_view_t v;
    sh_hap_view(in, &v);
    return v;
}

/* ---- Active <-> the vacation ------------------------------------------------------------ */

static void test_active_vacation(void)
{
    /* the vacation of the menu / the web interface -> Active */
    sh_hap_in_t in = normal();
    CHECK_EQ(view_of(&in).active, 1);
    in.vacation = true;
    CHECK_EQ(view_of(&in).active, 0);                           /* "off" in Apple Home = the vacation */
    in.vacation = false;
    CHECK_EQ(view_of(&in).active, 1);

    /* Active of a controller -> the vacation */
    bool vac = false;
    CHECK(sh_hap_write_active(0, &vac));
    CHECK(vac);
    CHECK(sh_hap_write_active(1, &vac));
    CHECK(!vac);
    vac = true;
    CHECK(!sh_hap_write_active(2, &vac));                       /* refused: nothing changes */
    CHECK(!sh_hap_write_active(-1, &vac));
    CHECK(vac);

    /* the target state is Heat and nothing else */
    CHECK(sh_hap_write_target(SH_HAP_TARGET_HEAT));
    CHECK_EQ(SH_HAP_TARGET_HEAT, 1);
    CHECK(!sh_hap_write_target(0));                             /* Auto */
    CHECK(!sh_hap_write_target(2));                             /* Cool: the cooling tower, not now */
}

/* ---- Heating Threshold = Tmax, 30 ... 85 °C --------------------------------------------- */

static void test_tmax(void)
{
    sh_hap_in_t in = normal();
    CHECK_EQ(view_of(&in).threshold, 70);
    in.tmax = 55;                                               /* tbo of the menu / the web interface */
    CHECK_EQ(view_of(&in).threshold, 55);

    /* the limits are those of the menu value tbo */
    CHECK_EQ(sh_setting_info[SH_SET_TANK_MAX].min, 30);
    CHECK_EQ(sh_setting_info[SH_SET_TANK_MAX].max, 85);
    CHECK_EQ(sh_setting_info[SH_SET_TANK_MAX].step, 1);

    int16_t t = 0;
    CHECK(sh_hap_write_threshold(30.0f, &t));
    CHECK_EQ(t, 30);
    CHECK(sh_hap_write_threshold(85.0f, &t));
    CHECK_EQ(t, 85);
    CHECK(sh_hap_write_threshold(60.0f, &t));
    CHECK_EQ(t, 60);
    CHECK(sh_settings_value_ok(SH_SET_TANK_MAX, t));
    /* whole degrees: a value between the steps is rounded */
    CHECK(sh_hap_write_threshold(60.4f, &t));
    CHECK_EQ(t, 60);
    CHECK(sh_hap_write_threshold(60.5f, &t));
    CHECK_EQ(t, 61);
    CHECK(sh_hap_write_threshold(29.6f, &t));
    CHECK_EQ(t, 30);
    CHECK(sh_hap_write_threshold(85.4f, &t));
    CHECK_EQ(t, 85);
    /* out of the range: refused, the value stays */
    t = 70;
    CHECK(!sh_hap_write_threshold(29.0f, &t));
    CHECK(!sh_hap_write_threshold(86.0f, &t));
    CHECK(!sh_hap_write_threshold(0.0f, &t));
    CHECK(!sh_hap_write_threshold(25.0f, &t));                  /* the top of the HAP default range */
    CHECK(!sh_hap_write_threshold(-10.0f, &t));
    CHECK(!sh_hap_write_threshold(1000.0f, &t));
    CHECK(!sh_hap_write_threshold(NAN, &t));
    CHECK_EQ(t, 70);
}

/* ---- Current Heater Cooler State -------------------------------------------------------- */

static void test_current_state(void)
{
    CHECK_EQ(SH_HAP_IDLE, 1);                                   /* the values of HAP */
    CHECK_EQ(SH_HAP_HEATING, 2);
    CHECK_EQ(SH_HAP_COOLING, 3);

    /* the pump stands: Idle, whatever the reason — and in the vacation too */
    for (int r = 0; r < SH_PUMP_REASON_COUNT; r++) {
        sh_hap_in_t in = normal();
        in.reason = (sh_pump_reason_t)r;
        CHECK_EQ(view_of(&in).cur_state, SH_HAP_IDLE);
        in.vacation = true;
        CHECK_EQ(view_of(&in).cur_state, SH_HAP_IDLE);
    }
    /* the pump runs: Cooling by the night cooling, Heating otherwise */
    for (int r = 0; r < SH_PUMP_REASON_COUNT; r++) {
        sh_hap_in_t in = normal();
        in.pump_on = true;
        in.reason = (sh_pump_reason_t)r;
        CHECK_EQ(view_of(&in).cur_state, r == SH_PUMP_REASON_COOL ? SH_HAP_COOLING : SH_HAP_HEATING);
    }
    sh_hap_in_t in = normal();
    in.pump_on = true;                                          /* charging by the difference */
    CHECK_EQ(view_of(&in).cur_state, SH_HAP_HEATING);
    in.reason = SH_PUMP_REASON_COL_PROTECT;                     /* the collector protection charges too */
    CHECK_EQ(view_of(&in).cur_state, SH_HAP_HEATING);
    in.vacation = true;                                         /* the vacation at night: cooling */
    in.reason = SH_PUMP_REASON_COOL;
    CHECK_EQ(view_of(&in).cur_state, SH_HAP_COOLING);
    CHECK_EQ(view_of(&in).active, 0);
}

/* ---- the temperatures and their ranges -------------------------------------------------- */

static void test_temperatures(void)
{
    int16_t t = 0;
    /* the tank (Current Temperature of the Heater Cooler, "Tank"): 0 ... 100 °C */
    CHECK(sh_hap_temp(418, SH_HAP_TANK_MIN, SH_HAP_TANK_MAX, &t));
    CHECK_EQ(t, 418);
    CHECK(sh_hap_temp(-25, SH_HAP_TANK_MIN, SH_HAP_TANK_MAX, &t));
    CHECK_EQ(t, 0);
    CHECK(sh_hap_temp(1042, SH_HAP_TANK_MIN, SH_HAP_TANK_MAX, &t));
    CHECK_EQ(t, 1000);
    CHECK_EQ(SH_HAP_TANK_MIN, 0);
    CHECK_EQ(SH_HAP_TANK_MAX, 1000);
    /* "Collector": -40 ... 200 °C (the HAP default 0 ... 100 °C is too narrow) */
    CHECK_EQ(SH_HAP_COL_MIN, -400);
    CHECK_EQ(SH_HAP_COL_MAX, 2000);
    CHECK(sh_hap_temp(1305, SH_HAP_COL_MIN, SH_HAP_COL_MAX, &t));
    CHECK_EQ(t, 1305);
    CHECK(sh_hap_temp(-123, SH_HAP_COL_MIN, SH_HAP_COL_MAX, &t));
    CHECK_EQ(t, -123);
    CHECK(sh_hap_temp(-450, SH_HAP_COL_MIN, SH_HAP_COL_MAX, &t));
    CHECK_EQ(t, -400);
    CHECK(sh_hap_temp(2100, SH_HAP_COL_MIN, SH_HAP_COL_MAX, &t));
    CHECK_EQ(t, 2000);
    /* no value yet (boot; the pulse mode before the first pulse): the characteristic keeps its own */
    t = 77;
    CHECK(!sh_hap_temp(SH_TEMP_INVALID, SH_HAP_TANK_MIN, SH_HAP_TANK_MAX, &t));
    CHECK(!sh_hap_temp(SH_TEMP_INVALID, SH_HAP_COL_MIN, SH_HAP_COL_MAX, &t));
    CHECK_EQ(t, 77);
}

/* ---- Temperature Display Units: our display; HomeKit always gets °C ---------------------- */

static void test_units(void)
{
    bool f = false;
    CHECK(sh_hap_write_units(SH_HAP_UNITS_F, &f));
    CHECK(f);
    CHECK(sh_hap_write_units(SH_HAP_UNITS_C, &f));
    CHECK(!f);
    CHECK_EQ(SH_HAP_UNITS_C, 0);
    CHECK_EQ(SH_HAP_UNITS_F, 1);
    f = true;
    CHECK(!sh_hap_write_units(2, &f));
    CHECK(f);

    /* the display: °C as it is, °F converted (tenths) */
    CHECK_EQ(sh_hap_display_temp(418, false), 418);
    CHECK_EQ(sh_hap_display_temp(-99, false), -99);
    CHECK_EQ(sh_hap_display_temp(0, true), 320);                /* 0.0 °C = 32.0 °F */
    CHECK_EQ(sh_hap_display_temp(1000, true), 2120);            /* 100 °C = 212 °F */
    CHECK_EQ(sh_hap_display_temp(700, true), 1580);             /* 70 °C = 158 °F */
    CHECK_EQ(sh_hap_display_temp(418, true), 1072);             /* 41.8 °C = 107.24 °F */
    CHECK_EQ(sh_hap_display_temp(-400, true), -400);            /* -40 is the same in both */
    CHECK_EQ(sh_hap_display_temp(-100, true), 140);             /* -10 °C = 14 °F */
    CHECK_EQ(sh_hap_display_temp(-178, true), 0);               /* -17.8 °C = -0.04 °F */
    CHECK_EQ(sh_hap_display_temp(1250, true), 2570);            /* the top of DS18B20 */
    CHECK_EQ(sh_hap_display_temp(SH_TEMP_INVALID, true), SH_TEMP_INVALID);
    CHECK_EQ(sh_hap_display_temp(SH_TEMP_INVALID, false), SH_TEMP_INVALID);

    /* the units are not an input of what HomeKit gets: the same °C whatever the display shows */
    int16_t t = 0;
    CHECK(sh_hap_temp(418, SH_HAP_TANK_MIN, SH_HAP_TANK_MAX, &t));
    CHECK_EQ(t, 418);
    sh_hap_in_t in = normal();
    CHECK_EQ(view_of(&in).threshold, 70);                       /* Tmax in °C */
}

/* ---- Lock Physical Controls: the child lock --------------------------------------------- */

typedef struct {
    sh_menu_t m;
    uint32_t now;
    sh_menu_ctx_t ctx;
} ui_t;

static sh_menu_act_t key(ui_t *u, sh_key_t k, uint32_t dt);
static sh_menu_view_t view(ui_t *u, uint32_t dt);
static char s_json[SH_WEB_STATUS_MAX];

/* ---- °F: only what is shown and entered; inside everything stays °C / K ------------------ */

static void test_display_units(void)
{
    const sh_hap_qty_t T = SH_HAP_QTY_TEMP, D = SH_HAP_QTY_DIFF, N = SH_HAP_QTY_NONE;

    /* °C chosen (or nothing chosen): nothing is converted */
    CHECK_EQ(sh_hap_display_whole(70, T, false), 70);
    CHECK_EQ(sh_hap_display_whole(6, D, false), 6);
    CHECK_EQ(sh_hap_entered_whole(70, T, false), 70);
    CHECK_EQ(sh_hap_entered_whole(6, D, false), 6);

    /* a temperature: * 1.8 + 32, rounded to a whole °F */
    CHECK_EQ(sh_hap_display_whole(0, T, true), 32);
    CHECK_EQ(sh_hap_display_whole(100, T, true), 212);
    CHECK_EQ(sh_hap_display_whole(70, T, true), 158);
    CHECK_EQ(sh_hap_display_whole(25, T, true), 77);
    CHECK_EQ(sh_hap_display_whole(31, T, true), 88);            /* 87.8 */
    CHECK_EQ(sh_hap_display_whole(33, T, true), 91);            /* 91.4 */
    CHECK_EQ(sh_hap_display_whole(-40, T, true), -40);
    CHECK_EQ(sh_hap_display_whole(-18, T, true), 0);            /* -0.4 */
    CHECK_EQ(sh_hap_display_whole(-19, T, true), -2);           /* -2.2 */
    CHECK_EQ(sh_hap_display_whole(250, T, true), 482);
    /* a difference: * 1.8 only, no + 32 */
    CHECK_EQ(sh_hap_display_whole(0, D, true), 0);
    CHECK_EQ(sh_hap_display_whole(1, D, true), 2);              /* 1.8 */
    CHECK_EQ(sh_hap_display_whole(2, D, true), 4);              /* 3.6 */
    CHECK_EQ(sh_hap_display_whole(3, D, true), 5);              /* 5.4 */
    CHECK_EQ(sh_hap_display_whole(4, D, true), 7);              /* 7.2 */
    CHECK_EQ(sh_hap_display_whole(5, D, true), 9);
    CHECK_EQ(sh_hap_display_whole(6, D, true), 11);             /* 10.8 */
    CHECK_EQ(sh_hap_display_whole(20, D, true), 36);
    /* anything else (seconds, on / off) is never converted */
    CHECK_EQ(sh_hap_display_whole(300, N, true), 300);
    CHECK_EQ(sh_hap_entered_whole(300, N, true), 300);

    /* which menu value is what */
    CHECK_EQ(sh_hap_setting_qty(SH_SET_TANK_MAX), T);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_COL_MIN), T);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_COL_MIN_W), T);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_DIFF_ON), D);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_DIFF_OFF), D);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_DIFF_ON_W), D);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_DIFF_OFF_W), D);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_TEST_S), N);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_SEASON), N);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_VACATION), N);
    CHECK_EQ(sh_hap_setting_qty(SH_SET_NCOOL), N);

    /* the step stays 1 °C / 1 K of the stored value: in °F the shown values step unevenly */
    const int16_t tbo_f[] = { 86, 88, 90, 91, 93, 95, 97, 99, 100, 102 };
    for (int i = 0; i < 10; i++) {
        CHECK_EQ(sh_hap_display_whole((int16_t)(30 + i), T, true), tbo_f[i]);
    }
    CHECK_EQ(sh_hap_display_whole(85, T, true), 185);           /* the top of tbo: 3 digits */

    /* entering: the nearest whole °C / K; what is shown comes back as the same stored value —
     * over every range a page or the menu has (tbo, C.Lo, the collector maximum, d.on, d.oF) */
    for (int16_t c = -40; c <= 250; c++) {
        const int16_t f = sh_hap_display_whole(c, T, true);
        CHECK_EQ(sh_hap_entered_whole(f, T, true), c);
        CHECK(f > sh_hap_display_whole((int16_t)(c - 1), T, true));     /* no two values look the same */
    }
    for (int16_t k = 0; k <= 30; k++) {
        CHECK_EQ(sh_hap_entered_whole(sh_hap_display_whole(k, D, true), D, true), k);
    }
    CHECK_EQ(sh_hap_entered_whole(140, T, true), 60);
    CHECK_EQ(sh_hap_entered_whole(141, T, true), 61);           /* 60.6 */
    CHECK_EQ(sh_hap_entered_whole(150, T, true), 66);           /* 65.6 */
    CHECK_EQ(sh_hap_entered_whole(32, T, true), 0);
    CHECK_EQ(sh_hap_entered_whole(10, D, true), 6);             /* 5.6 */
    CHECK_EQ(sh_hap_entered_whole(9, D, true), 5);

    /* the temperatures with tenths: to 0.1 °F */
    CHECK_EQ(sh_hap_display_temp(523, true), 1261);             /* 52.3 °C = 126.14 °F */
    CHECK_EQ(sh_hap_display_temp(1, true), 322);                /* 0.1 °C = 32.18 °F */
    CHECK_EQ(sh_hap_display_temp(-1, true), 318);

    /* the logic gets the same values in °C and in °F: the menu stores the same whatever the
     * display shows — the units are no input of it */
    ui_t u = { .now = 1000, .ctx = { .hap_on = true } };
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_PLUS, 100);
    key(&u, SH_KEY_PLUS, 100);                                  /* tbo */
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(sh_menu_setting(view(&u, 0).pos, false), SH_SET_TANK_MAX);
    const int16_t shown_f[] = { 158, 160, 162, 163 };           /* 70, 71, 72, 73 °C */
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(view(&u, 0).value, 70 + i);                    /* the value of the menu: °C */
        CHECK_EQ(sh_hap_display_whole(view(&u, 0).value, sh_hap_setting_qty(SH_SET_TANK_MAX), true), shown_f[i]);
        CHECK_EQ(sh_hap_display_whole(view(&u, 0).value, sh_hap_setting_qty(SH_SET_TANK_MAX), false), 70 + i);
        if (i < 3) {
            key(&u, SH_KEY_PLUS, 100);
        }
    }
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_SAVE);
    CHECK_EQ(u.m.value, 73);                                    /* stored in °C */
    CHECK_EQ(u.m.id, SH_SET_TANK_MAX);
    /* HomeKit and the rules behind it: the units are not among their inputs */
    sh_hap_in_t hin = normal();
    CHECK_EQ(view_of(&hin).threshold, 70);

    /* the web interface: the same data whatever the units — only the flag for the page differs */
    sh_web_status_t s = {
        .t_col = 523, .t_tank = 418,
        .cfg = { .settings = S, .frost = true,
                 .inst = { .v = { SH_FLUID_WATER, 0, 15, 120, SH_COL_FLAT, 20, 150, 20 } } },
        .col_protect = 1100, .name = "Solar-810840", .version = "0.1.0", .serial = "E8F60A810840",
        .ssid = "Home", .ip = "192.168.1.7", .rssi = -41, .host = "Solar-810840", .ap_ssid = "Solar-810840",
        .clock = { .trust = SH_CLOCK_UNSET, .min = -1, .tz = "UTC", .peak = SH_PEAK_NONE },
        .cool_until = -1, .sunrise = -1, .sunset = -1, .e8_normal = INT16_MIN,
    };
    static char c_json[SH_WEB_STATUS_MAX];
    CHECK(sh_web_status_json(c_json, sizeof(c_json), &s) > 0);
    CHECK(strstr(c_json, "\"win\":0,\"f\":0},") != NULL);
    s.fahrenheit = true;
    CHECK(sh_web_status_json(s_json, sizeof(s_json), &s) > 0);
    char *flag = strstr(s_json, "\"f\":1},");
    CHECK(flag != NULL);
    if (flag) {
        flag[4] = '0';
    }
    CHECK(strcmp(s_json, c_json) == 0);
    CHECK(strstr(c_json, "\"tank\":418,\"col\":523,") != NULL);                     /* tenths of °C */
    CHECK(strstr(c_json, "\"tbo\":{\"v\":70,\"min\":30,\"max\":85,\"step\":1}") != NULL);
    /* and a request is in °C / K: the page converts before it sends */
    sh_web_config_t cfg = s.cfg;
    unsigned ch = 0;
    CHECK_EQ(sh_web_set("tbo=66", CODE, &cfg, &ch), SH_WEB_OK);
    CHECK_EQ(cfg.settings.v[SH_SET_TANK_MAX], 66);
    CHECK_EQ(sh_web_set("tbo=150", CODE, &cfg, &ch), SH_WEB_BAD);                   /* °F is not taken */
}

static sh_menu_act_t key(ui_t *u, sh_key_t k, uint32_t dt)
{
    u->now += dt;
    return sh_menu_key(&u->m, k, u->now, &u->ctx, &S);
}

static sh_menu_view_t view(ui_t *u, uint32_t dt)
{
    u->now += dt;
    sh_menu_update(&u->m, u->now);
    sh_menu_view_t v;
    sh_menu_view(&u->m, u->now, &v);
    return v;
}

/* in the list at LoC, the keyboard unlocked */
static ui_t at_loc(void)
{
    ui_t u = { .now = 1000, .ctx = { .hap_on = true } };
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MINUS, 100);                                 /* SSn -> End */
    key(&u, SH_KEY_MINUS, 100);                                 /* End -> LoC */
    return u;
}

/* "+" and "-" go down at `t0` and stay: the time at which their hold is reported, 0 if never
 * within 10 s (polled every 50 ms, as sh_buttons does). */
static uint32_t chord_fires(sh_chord_t *c, uint32_t t0)
{
    for (uint32_t t = t0; t <= t0 + 10000; t += 50) {
        if (sh_chord_step(c, t)) {
            return t;
        }
    }
    return 0;
}

static void test_lock(void)
{
    /* one variable, whoever sets it: a change is reported once (stored, shown, notified) */
    bool lock = false;
    CHECK(sh_hap_lock_change(&lock, true));                     /* e.g. the buttons */
    CHECK(lock);
    CHECK(!sh_hap_lock_change(&lock, true));                    /* already locked: nothing to do */
    CHECK(sh_hap_lock_change(&lock, false));                    /* e.g. Apple Home unlocks it */
    CHECK(!lock);
    CHECK(!sh_hap_lock_change(&lock, false));
    CHECK(sh_hap_lock_change(&lock, true));                     /* the web interface */
    CHECK(sh_hap_lock_change(&lock, false));                    /* the buttons again */
    /* the value of a controller */
    CHECK(sh_hap_write_lock(1, &lock));
    CHECK(lock);
    CHECK(sh_hap_write_lock(0, &lock));
    CHECK(!lock);
    CHECK(!sh_hap_write_lock(2, &lock));
    /* HomeKit shows it and is notified of every change, whatever the source */
    sh_hap_in_t in = normal();
    sh_hap_view_t sent = view_of(&in);
    CHECK_EQ(sent.lock, 0);
    in.lock = true;
    sh_hap_view_t now = view_of(&in);
    CHECK_EQ(now.lock, 1);
    CHECK_EQ(sh_hap_changed(&sent, &now), SH_HAP_CH_LOCK);
    sent = now;
    in.lock = false;
    now = view_of(&in);
    CHECK_EQ(sh_hap_changed(&sent, &now), SH_HAP_CH_LOCK);

    /* ---- locked: no button does anything, a press shows "LoC" for 1 s ---- */
    ui_t u = { .now = 1000, .ctx = { .locked = true } };
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_NONE);
    CHECK_EQ(key(&u, SH_KEY_MENU, 0), SH_MENU_ACT_NONE);        /* no menu */
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    CHECK_EQ(view(&u, 999).notice, SH_MENU_NOTICE_LOC);
    CHECK_EQ(view(&u, 1).notice, SH_MENU_NOTICE_NONE);          /* 1 s */
    CHECK_EQ(SH_MENU_NOTICE_MS, 1000);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 5000), SH_MENU_ACT_NONE);     /* no pump test */
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    CHECK_EQ(key(&u, SH_KEY_MINUS, 5000), SH_MENU_ACT_NONE);
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    CHECK_EQ(key(&u, SH_KEY_PLUS_REPEAT, 5000), SH_MENU_ACT_NONE);  /* a held button too */
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    u.ctx.test_running = true;                                  /* a test started from the web interface */
    CHECK_EQ(key(&u, SH_KEY_PLUS, 5000), SH_MENU_ACT_NONE);     /* no +60 s */
    CHECK_EQ(key(&u, SH_KEY_MINUS, 0), SH_MENU_ACT_NONE);       /* no -60 s */
    CHECK_EQ(key(&u, SH_KEY_MENU, 0), SH_MENU_ACT_NONE);        /* no stop */
    u.ctx.test_running = false;
    u.ctx.air_running = true;
    CHECK_EQ(key(&u, SH_KEY_MENU, 5000), SH_MENU_ACT_NONE);     /* the air venting is not stopped */
    u.ctx.air_running = false;
    u.ctx.fup_offer = true;
    CHECK_EQ(key(&u, SH_KEY_MENU, 5000), SH_MENU_ACT_NONE);     /* "FUp" is not confirmed */
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    u.ctx.fup_offer = false;
    /* the resets by holding "menu" (5 s Wi-Fi, 9 s factory) */
    view(&u, 5000);
    CHECK(!sh_menu_hold(&u.m, u.now, &u.ctx));
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    u.ctx.locked = false;
    view(&u, 5000);
    CHECK(sh_menu_hold(&u.m, u.now, &u.ctx));                   /* unlocked: as before */
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_NONE);

    /* ---- "+" and "-" held together for 3 s ---- */
    CHECK_EQ(SH_MENU_CHORD_MS, 3000);
    sh_chord_t c = { 0 };
    sh_chord_button(&c, true, true, 1000);                      /* "+" alone */
    CHECK(!sh_chord_holding(&c));
    CHECK(!sh_chord_swallow(&c, true));                         /* an ordinary "+" */
    CHECK_EQ(chord_fires(&c, 1000), 0);
    sh_chord_button(&c, false, true, 1400);                     /* "-" joins: the 3 s start here */
    CHECK(sh_chord_holding(&c));
    CHECK(!sh_chord_step(&c, 4399));
    CHECK(sh_chord_step(&c, 4400));
    CHECK(!sh_chord_step(&c, 4450));                            /* once */
    CHECK(!sh_chord_step(&c, 60000));
    /* neither press is a key: no tap, no repetition — also after the release */
    CHECK(sh_chord_swallow(&c, true));
    CHECK(sh_chord_swallow(&c, false));
    sh_chord_button(&c, true, false, 5000);
    sh_chord_button(&c, false, false, 5100);
    CHECK(sh_chord_swallow(&c, true));                          /* the taps come at the release */
    CHECK(sh_chord_swallow(&c, false));
    CHECK(!sh_chord_holding(&c));
    /* the next single press is a key again */
    sh_chord_button(&c, true, true, 6000);
    CHECK(!sh_chord_swallow(&c, true));
    sh_chord_button(&c, true, false, 6100);
    /* released before the 3 s: nothing, and still no "+" / "-" (no pump test by a slip) */
    sh_chord_button(&c, false, true, 7000);
    sh_chord_button(&c, true, true, 7100);
    CHECK(!sh_chord_step(&c, 9000));
    sh_chord_button(&c, true, false, 9500);
    CHECK(!sh_chord_step(&c, 12000));
    CHECK(sh_chord_swallow(&c, true));
    CHECK(sh_chord_swallow(&c, false));
    /* one of them released and pressed again: the 3 s start again */
    sh_chord_button(&c, true, true, 10000);
    CHECK(!sh_chord_step(&c, 12999));
    CHECK(sh_chord_step(&c, 13000));
    sh_chord_button(&c, true, false, 13100);
    sh_chord_button(&c, false, false, 13100);
    /* polled as the buttons task does */
    sh_chord_button(&c, false, true, 20000);
    sh_chord_button(&c, true, true, 20020);
    CHECK_EQ(chord_fires(&c, 20050), 23050);

    /* what the hold does: locked -> unlocks, always, whatever is shown */
    u = (ui_t){ .now = 1000, .ctx = { .locked = true } };
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_UNLOCK);
    u.ctx.test_running = true;
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_UNLOCK);
    u.ctx.test_running = false;
    u.ctx.air_running = true;
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_UNLOCK);
    u.ctx.air_running = false;
    u.ctx.fup_offer = true;
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_UNLOCK);
    /* unlocked -> locks, from the normal screen only */
    u = (ui_t){ .now = 1000 };
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_LOCK);
    u.ctx.test_running = true;                                  /* during the pump test: nothing */
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_NONE);
    u.ctx.test_running = false;
    u.ctx.air_running = true;                                   /* the air venting screen: nothing */
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_NONE);
    u.ctx.air_running = false;
    key(&u, SH_KEY_MENU, 0);                                    /* in the list: nothing */
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_NONE);
    key(&u, SH_KEY_PLUS, 100);
    key(&u, SH_KEY_PLUS, 100);
    key(&u, SH_KEY_MENU, 100);                                  /* tbo blinks: nothing, the value stays */
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    CHECK_EQ(sh_menu_chord(&u.m, &u.ctx), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.value, 70);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);

    /* ---- the menu position LoC: the last one before End ---- */
    CHECK_EQ(SH_MENU_LOC, SH_MENU_HAP + 1);
    CHECK_EQ(SH_MENU_END, SH_MENU_LOC + 1);
    u = at_loc();
    CHECK_EQ(u.m.pos, SH_MENU_LOC);
    /* always opens with "oFF" blinking */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    CHECK_EQ(view(&u, 0).pos, SH_MENU_LOC);
    CHECK_EQ(view(&u, 0).value, 0);
    CHECK(!view(&u, 500).value_visible);
    /* opened by chance: "menu" leaves without locking */
    CHECK_EQ(key(&u, SH_KEY_MENU, 600), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_SAVED);                          /* "oFF" steady for 1 s, then the list */
    CHECK_EQ(view(&u, 1000).mode, SH_MENU_LIST);
    CHECK_EQ(u.m.pos, SH_MENU_LOC);
    /* "on" and back to "oFF", no wrap */
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.value, 1);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.value, 1);
    CHECK_EQ(key(&u, SH_KEY_MINUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.value, 0);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);      /* "oFF" confirmed: not locked */
    /* only "on" + "menu" locks, and the menu closes */
    view(&u, 1000);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(view(&u, 0).value, 0);                             /* "oFF" again */
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_LOCK);
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    /* "on" not confirmed: 10 s later the normal screen, not locked */
    u = at_loc();
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(view(&u, SH_MENU_TIMEOUT_MS).mode, SH_MENU_NORMAL);

    /* ---- the display at a change, from any source ---- */
    u = (ui_t){ .now = 1000 };
    sh_menu_lock_changed(&u.m, true, u.now);                    /* locked */
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    CHECK_EQ(view(&u, 999).notice, SH_MENU_NOTICE_LOC);
    CHECK_EQ(view(&u, 1).notice, SH_MENU_NOTICE_NONE);          /* then what was shown goes on */
    sh_menu_lock_changed(&u.m, false, u.now);                   /* unlocked */
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_UNL);
    CHECK_EQ(view(&u, 999).notice, SH_MENU_NOTICE_UNL);
    CHECK_EQ(view(&u, 1).notice, SH_MENU_NOTICE_NONE);
    /* the test and the air venting are not touched: the notice is only shown over their screen */
    u.ctx.test_running = true;
    sh_menu_lock_changed(&u.m, true, u.now);
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    u.ctx.test_running = false;
    view(&u, 2000);
    sh_menu_lock_changed(&u.m, false, u.now);
    /* locked from Apple Home / the web interface while the menu is open: "LoC" and the normal
     * screen at once, the unconfirmed value is lost */
    view(&u, 2000);
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_PLUS, 100);
    key(&u, SH_KEY_PLUS, 100);
    key(&u, SH_KEY_MENU, 100);                                  /* tbo */
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_NONE);      /* 71, not confirmed */
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    sh_menu_lock_changed(&u.m, true, u.now);
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    CHECK_EQ(view(&u, 0).mode, SH_MENU_NORMAL);
    CHECK_EQ(view(&u, 0).notice, SH_MENU_NOTICE_LOC);
    u.ctx.locked = true;
    CHECK_EQ(key(&u, SH_KEY_MENU, 2000), SH_MENU_ACT_NONE);     /* and it stays closed */
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    /* unlocked while the menu is open cannot happen (a lock closes it); an unlock leaves the mode */
    u.ctx.locked = false;
    sh_menu_lock_changed(&u.m, false, u.now);
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 2000), SH_MENU_ACT_PLUS);     /* the buttons work again */

    /* ---- the web interface: the same variable, with the access code ---- */
    bool on = false;
    CHECK_EQ(sh_web_lock("on=1&code=" CODE, CODE, &on), SH_WEB_OK);
    CHECK(on);
    CHECK_EQ(sh_web_lock("code=" CODE "&on=0", CODE, &on), SH_WEB_OK);
    CHECK(!on);
    CHECK_EQ(sh_web_lock("on=1", CODE, &on), SH_WEB_DENIED);
    CHECK_EQ(sh_web_lock("on=1&code=000000", CODE, &on), SH_WEB_DENIED);
    CHECK_EQ(sh_web_lock("on=2&code=" CODE, CODE, &on), SH_WEB_BAD);
    CHECK_EQ(sh_web_lock("code=" CODE, CODE, &on), SH_WEB_BAD);
    /* the web interface itself is not locked: its requests do not know the lock */
    sh_web_test_t cmd = SH_WEB_TEST_NONE;
    CHECK_EQ(sh_web_test("cmd=start", false, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_TEST_PLUS);
}

/* ---- Status Fault by the alarms --------------------------------------------------------- */

static void test_status_fault(void)
{
    const struct {
        sh_alarm_t alarm;
        uint8_t col, tank;
    } rows[] = {
        { SH_ALARM_E1, 1, 0 },          /* collector sensor faulty */
        { SH_ALARM_E5, 1, 0 },          /* collector above the boiling point */
        { SH_ALARM_E2, 0, 1 },          /* tank sensor faulty */
        { SH_ALARM_E4, 0, 1 },          /* tank at the emergency ceiling */
        { SH_ALARM_E6, 0, 0 },          /* the web interface and the display only */
        { SH_ALARM_E8, 0, 0 },
        { SH_ALARM_HOT, 0, 0 },
        { SH_ALARM_FRO, 0, 0 },
    };
    CHECK_EQ(sizeof(rows) / sizeof(rows[0]), SH_ALARM_COUNT);
    sh_hap_in_t in = normal();
    CHECK_EQ(view_of(&in).col_fault, 0);
    CHECK_EQ(view_of(&in).tank_fault, 0);
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        in.alarms = (uint8_t)BIT(rows[i].alarm);
        CHECK_EQ(view_of(&in).col_fault, rows[i].col);
        CHECK_EQ(view_of(&in).tank_fault, rows[i].tank);
    }
    in.alarms = (uint8_t)(BIT(SH_ALARM_E1) | BIT(SH_ALARM_E2));   /* both sensors */
    CHECK_EQ(view_of(&in).col_fault, 1);
    CHECK_EQ(view_of(&in).tank_fault, 1);
    in.alarms = (uint8_t)(BIT(SH_ALARM_E4) | BIT(SH_ALARM_E5) | BIT(SH_ALARM_E6));
    CHECK_EQ(view_of(&in).col_fault, 1);
    CHECK_EQ(view_of(&in).tank_fault, 1);
    in.alarms = 0;                                              /* cleared */
    CHECK_EQ(view_of(&in).col_fault, 0);
    CHECK_EQ(view_of(&in).tank_fault, 0);
}

/* ---- the notifications: when one goes out and when not ---------------------------------- */

static unsigned step(sh_hap_view_t *sent, const sh_hap_in_t *in)
{
    const sh_hap_view_t now = view_of(in);
    const unsigned ch = sh_hap_changed(sent, &now);
    *sent = now;
    return ch;
}

static void test_notifications(void)
{
    sh_hap_in_t in = normal();
    sh_hap_view_t sent = view_of(&in);
    CHECK_EQ(step(&sent, &in), 0);                              /* nothing changed: nothing is sent */

    /* the pump starts charging: the Current State */
    in.pump_on = true;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_CUR_STATE);
    CHECK_EQ(step(&sent, &in), 0);                              /* once */
    /* another reason of the same state: nothing */
    in.reason = SH_PUMP_REASON_MIN_RUN;
    CHECK_EQ(step(&sent, &in), 0);
    in.pump_on = false;
    in.reason = SH_PUMP_REASON_MIN_OFF;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_CUR_STATE);
    in.reason = SH_PUMP_REASON_DIFF;                            /* still standing */
    CHECK_EQ(step(&sent, &in), 0);
    in.pump_on = true;                                          /* the night cooling */
    in.reason = SH_PUMP_REASON_COOL;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_CUR_STATE);
    in.pump_on = false;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_CUR_STATE);

    /* the vacation from the menu / the web interface: Active */
    in.vacation = true;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_ACTIVE);
    CHECK_EQ(step(&sent, &in), 0);
    in.vacation = false;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_ACTIVE);

    /* Tmax from the menu / the web interface: the Heating Threshold */
    in.tmax = 65;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_THRESHOLD);
    CHECK_EQ(step(&sent, &in), 0);

    /* the child lock */
    in.lock = true;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_LOCK);
    in.lock = false;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_LOCK);

    /* the Status Fault of each sensor */
    in.alarms = (uint8_t)BIT(SH_ALARM_E1);
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_COL_FAULT);
    in.alarms |= (uint8_t)BIT(SH_ALARM_E5);                     /* a second reason of the same fault */
    CHECK_EQ(step(&sent, &in), 0);
    in.alarms = (uint8_t)BIT(SH_ALARM_E4);
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_COL_FAULT | SH_HAP_CH_TANK_FAULT);
    in.alarms = 0;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_TANK_FAULT);
    /* the alarms without a Status Fault and the information codes: nothing */
    in.alarms = (uint8_t)(BIT(SH_ALARM_E6) | BIT(SH_ALARM_E8) | BIT(SH_ALARM_HOT) | BIT(SH_ALARM_FRO));
    CHECK_EQ(step(&sent, &in), 0);

    /* several at once */
    in.vacation = true;
    in.tmax = 60;
    in.pump_on = true;
    in.reason = SH_PUMP_REASON_COL_PROTECT;
    CHECK_EQ(step(&sent, &in), SH_HAP_CH_ACTIVE | SH_HAP_CH_THRESHOLD | SH_HAP_CH_CUR_STATE);

    /* the temperatures are not among the notified values at all: they are answered on a read
     * (sh_hap_temp()), so a changing temperature never sends anything */
    const unsigned all = SH_HAP_CH_ACTIVE | SH_HAP_CH_CUR_STATE | SH_HAP_CH_THRESHOLD | SH_HAP_CH_LOCK |
                         SH_HAP_CH_COL_FAULT | SH_HAP_CH_TANK_FAULT;
    const sh_hap_view_t a = { .active = 0, .cur_state = SH_HAP_IDLE, .threshold = 30 };
    const sh_hap_view_t b = { .active = 1, .cur_state = SH_HAP_HEATING, .threshold = 85, .lock = 1,
                              .col_fault = 1, .tank_fault = 1 };
    CHECK_EQ(sh_hap_changed(&a, &b), all);
    CHECK_EQ(all, 0x3F);                                        /* six characteristics, no temperature */
    CHECK_EQ(sh_hap_changed(&b, &b), 0);
}

/* ---- when HAP starts; which web server -------------------------------------------------- */

static void test_start(void)
{
    /* only with the setting on, only connected to the router, once */
    CHECK(sh_hap_start_due(true, true, false));
    CHECK(!sh_hap_start_due(false, true, false));               /* the setting is off */
    CHECK(!sh_hap_start_due(true, false, false));               /* no stored network / the portal / AP */
    CHECK(!sh_hap_start_due(true, true, true));                 /* already started: never again */
    CHECK(!sh_hap_start_due(false, false, false));
    CHECK(!sh_hap_start_due(true, false, true));                /* the connection lost: HAP is not stopped */

    /* the setting off: the own server on the router and on the own access point, as before */
    CHECK(sh_hap_own_web(false, false, true, false));
    CHECK(sh_hap_own_web(false, false, false, true));
    CHECK(!sh_hap_own_web(false, false, false, false));         /* no connection: no server */
    /* the setting on: on the router the server of HAP — never the own one */
    CHECK(!sh_hap_own_web(true, false, true, false));           /* HAP is about to start */
    CHECK(!sh_hap_own_web(true, true, true, false));
    CHECK(!sh_hap_own_web(true, false, false, false));
    /* AP before HAP was ever started (the router does not answer since boot): the own server */
    CHECK(sh_hap_own_web(true, false, false, true));
    /* once HAP is started its server stays (it is not stopped): also on the access point */
    CHECK(!sh_hap_own_web(true, true, false, true));
    CHECK(!sh_hap_own_web(true, true, false, false));
}

/* ---- the setting "Apple HomeKit": a restart; off is refused while paired ----------------- */

static ui_t at_hap(bool on, bool paired)
{
    ui_t u = { .now = 1000, .ctx = { .hap_on = on, .hap_paired = paired } };
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MINUS, 100);                                 /* SSn -> End */
    key(&u, SH_KEY_MINUS, 100);                                 /* End -> LoC */
    key(&u, SH_KEY_MINUS, 100);                                 /* LoC -> HAP */
    return u;
}

static void test_setting(void)
{
    CHECK_EQ(sh_hap_set(true, true, false), SH_HAP_SET_SAME);
    CHECK_EQ(sh_hap_set(false, false, false), SH_HAP_SET_SAME);
    CHECK_EQ(sh_hap_set(true, true, true), SH_HAP_SET_SAME);
    CHECK_EQ(sh_hap_set(true, false, false), SH_HAP_SET_RESTART);   /* stored, then a restart */
    CHECK_EQ(sh_hap_set(false, true, false), SH_HAP_SET_RESTART);
    CHECK_EQ(sh_hap_set(true, false, true), SH_HAP_SET_REFUSED);    /* first remove it from Apple Home */
    CHECK_EQ(sh_hap_set(false, true, true), SH_HAP_SET_RESTART);    /* on is never refused */

    /* the menu: HAP right after nEt */
    CHECK_EQ(SH_MENU_HAP, SH_MENU_NET + 1);
    ui_t u = at_hap(true, false);
    CHECK_EQ(u.m.pos, SH_MENU_HAP);
    CHECK_EQ(key(&u, SH_KEY_MINUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.pos, SH_MENU_NET);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.pos, SH_MENU_LOC);

    /* the value is the current setting, blinking; "+" on, "-" oFF, no wrap */
    u = at_hap(true, false);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    CHECK_EQ(view(&u, 0).pos, SH_MENU_HAP);
    CHECK_EQ(view(&u, 0).value, 1);
    CHECK(view(&u, 0).value_visible);
    CHECK(!view(&u, 500).value_visible);
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.value, 1);
    CHECK_EQ(key(&u, SH_KEY_MINUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.value, 0);
    CHECK_EQ(key(&u, SH_KEY_MINUS, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.value, 0);
    /* confirmed, not paired: passed on (stored, restart), "oFF" steady for 1 s */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_HAP);
    CHECK_EQ(u.m.value, 0);
    CHECK_EQ(u.m.mode, SH_MENU_SAVED);
    CHECK_EQ(view(&u, 999).mode, SH_MENU_SAVED);
    CHECK_EQ(view(&u, 0).code_part, 0);
    CHECK_EQ(view(&u, 1).mode, SH_MENU_LIST);
    CHECK_EQ(u.m.pos, SH_MENU_HAP);

    /* the same value: nothing is passed on */
    u = at_hap(true, false);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_SAVED);
    CHECK_EQ(u.m.value, 1);

    /* paired: oFF is refused — "on" steady for 1 s, nothing is passed on */
    u = at_hap(true, true);
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(view(&u, 0).value, 0);                             /* "oFF" blinks */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_SAVED);
    CHECK_EQ(view(&u, 0).value, 1);                             /* "on" */
    CHECK(view(&u, 0).value_visible);
    CHECK_EQ(view(&u, 1000).mode, SH_MENU_LIST);

    /* off -> on: passed on */
    u = at_hap(false, false);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(view(&u, 0).value, 0);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_HAP);
    CHECK_EQ(u.m.value, 1);

    /* an unconfirmed value is lost after 10 s */
    u = at_hap(true, false);
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(view(&u, SH_MENU_TIMEOUT_MS).mode, SH_MENU_NORMAL);

    /* the web interface: the same rule, with the access code */
    bool on = true;
    CHECK_EQ(sh_web_hap("on=0&code=" CODE, CODE, true, false, &on), SH_WEB_OK);
    CHECK(!on);
    CHECK_EQ(sh_web_hap("code=" CODE "&on=1", CODE, false, false, &on), SH_WEB_OK);
    CHECK(on);
    CHECK_EQ(sh_web_hap("on=1&code=" CODE, CODE, true, true, &on), SH_WEB_OK);      /* the same: fine */
    CHECK_EQ(sh_web_hap("on=0&code=" CODE, CODE, true, true, &on), SH_WEB_BAD);     /* paired: refused */
    CHECK_EQ(sh_web_hap("on=1&code=" CODE, CODE, false, true, &on), SH_WEB_OK);
    CHECK_EQ(sh_web_hap("on=0", CODE, true, false, &on), SH_WEB_DENIED);            /* never without the code */
    CHECK_EQ(sh_web_hap("on=0&code=000000", CODE, true, false, &on), SH_WEB_DENIED);
    CHECK_EQ(sh_web_hap("on=2&code=" CODE, CODE, true, false, &on), SH_WEB_BAD);
    CHECK_EQ(sh_web_hap("on=&code=" CODE, CODE, true, false, &on), SH_WEB_BAD);
    CHECK_EQ(sh_web_hap("code=" CODE, CODE, true, false, &on), SH_WEB_BAD);
}

/* ---- the pairing window; the LED breathes only while it is open -------------------------- */

static const sh_ind_params_t P = { .cold = 200, .hot = 900 };

static sh_ind_out_t ind(const sh_ind_in_t *in)
{
    sh_ind_state_t st = { 0 };
    return sh_ind_update(&st, &P, in);
}

static void test_pairing_window(void)
{
    /* HAP starts: open if no controller is paired */
    CHECK(sh_hap_window(false, SH_HAP_EVT_STARTED, 0));
    CHECK(!sh_hap_window(false, SH_HAP_EVT_STARTED, 1));
    CHECK(!sh_hap_window(false, SH_HAP_EVT_STARTED, 3));
    /* paired, or the 10 min are over: closed (HAP keeps running) */
    CHECK(!sh_hap_window(true, SH_HAP_EVT_PAIRED, 1));
    CHECK(!sh_hap_window(true, SH_HAP_EVT_TIMED_OUT, 0));
    CHECK(!sh_hap_window(false, SH_HAP_EVT_TIMED_OUT, 0));
    /* a second controller added, one of several removed: stays closed */
    CHECK(!sh_hap_window(false, SH_HAP_EVT_PAIRED, 2));
    CHECK(!sh_hap_window(false, SH_HAP_EVT_UNPAIRED, 1));
    /* the last controller removed: the SDK opens a new window */
    CHECK(sh_hap_window(false, SH_HAP_EVT_UNPAIRED, 0));

    /* the LED: breathing instead of steady, in the colour of the process */
    sh_ind_in_t in;
    memset(&in, 0, sizeof(in));
    in.t_col = 500;
    in.wifi_connected = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_STEADY);
    in.pair_window = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATHE);
    CHECK_EQ(ind(&in).color, SH_COLOR_BLUE);
    in.pump_on = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATHE);
    CHECK_EQ(ind(&in).color, SH_COLOR_GREEN);
    in.pump_on = false;
    in.vacation = true;
    CHECK_EQ(ind(&in).color, SH_COLOR_PURPLE);
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATHE);
    in.vacation = false;
    /* the window closes: steady again */
    in.pair_window = false;
    CHECK_EQ(ind(&in).pattern, SH_IND_STEADY);
    /* without the connection the heartbeat, on the own access point the wink — as before */
    in.pair_window = true;
    in.wifi_connected = false;
    CHECK_EQ(ind(&in).pattern, SH_IND_HEARTBEAT);
    in.wifi_portal = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_HEARTBEAT);
    in.wifi_portal = false;
    in.ap = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_WINK);
    in.ap = false;
    in.wifi_connected = true;
    /* the alarm patterns and the update come first */
    in.alarms = (uint8_t)BIT(SH_ALARM_E1);
    CHECK_EQ(ind(&in).pattern, SH_IND_DOUBLE);
    in.alarms = (uint8_t)BIT(SH_ALARM_E4);
    CHECK_EQ(ind(&in).pattern, SH_IND_FAST);
    in.alarms = 0;
    in.ota = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_BLINK);
    in.ota = false;
    /* an information code keeps the breathing, in orange */
    in.alarms = (uint8_t)BIT(SH_ALARM_HOT);
    in.pump_on = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATHE);
    CHECK_EQ(ind(&in).color, SH_COLOR_ORANGE);

    /* the breath: 0 -> the set brightness -> 0 in 5 s, then 0.4 s dark; never brighter */
    const uint32_t period = 2 * SH_IND_BREATHE_HALF_MS + SH_IND_BREATHE_DARK_MS;
    CHECK_EQ(period, 5400);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, 0), 0);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, SH_IND_BREATHE_HALF_MS), 100);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, SH_IND_BREATHE_HALF_MS / 2), 25);      /* squared ramp */
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, 2 * SH_IND_BREATHE_HALF_MS), 0);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, period - 10), 0);
    CHECK_EQ(sh_ind_level(SH_IND_BREATHE, period + SH_IND_BREATHE_HALF_MS), 100);
    uint8_t prev = 0;
    for (uint32_t ms = 0; ms <= SH_IND_BREATHE_HALF_MS; ms += 10) {             /* up, without a jump */
        const uint8_t l = sh_ind_level(SH_IND_BREATHE, ms);
        CHECK(l >= prev && l - prev <= 1);
        CHECK(l == sh_ind_level(SH_IND_BREATHE, 2 * SH_IND_BREATHE_HALF_MS - ms));   /* and down the same way */
        prev = l;
    }
    CHECK(!sh_ind_lit(SH_IND_BREATHE, 0));
    CHECK(sh_ind_lit(SH_IND_BREATHE, SH_IND_BREATHE_HALF_MS));
    /* the other patterns: all or nothing */
    CHECK_EQ(sh_ind_level(SH_IND_STEADY, 12345), 100);
    CHECK_EQ(sh_ind_level(SH_IND_HEARTBEAT, 0), 100);
    CHECK_EQ(sh_ind_level(SH_IND_HEARTBEAT, SH_IND_BEAT_ON_MS), 0);
    CHECK_EQ(sh_ind_level(SH_IND_WINK, 0), 0);
    CHECK_EQ(sh_ind_level(SH_IND_FAST, 0), 100);
    CHECK_EQ(sh_ind_level(SH_IND_BLINK, SH_IND_BLINK_MS), 0);
}

/* ---- Identify: 3 x 2 flashes of 200 ms, 300 ms between the groups (ledIdentify()) -------- */

static void test_identify(void)
{
    CHECK_EQ(SH_IND_IDENT_MS, 3000);
    unsigned flashes = 0;
    bool was = true;
    for (uint32_t ms = 0; ms < SH_IND_IDENT_MS + 500; ms++) {
        const bool lit = sh_ind_lit(SH_IND_IDENTIFY, ms);
        if (was && !lit) {
            flashes++;
        }
        was = lit;
    }
    CHECK_EQ(flashes, 6);
    /* the first group: dark 200, lit 200, dark 200, lit 200, then lit for the 300 ms of the gap */
    CHECK(!sh_ind_lit(SH_IND_IDENTIFY, 0));
    CHECK(!sh_ind_lit(SH_IND_IDENTIFY, 199));
    CHECK(sh_ind_lit(SH_IND_IDENTIFY, 200));
    CHECK(sh_ind_lit(SH_IND_IDENTIFY, 399));
    CHECK(!sh_ind_lit(SH_IND_IDENTIFY, 400));
    CHECK(sh_ind_lit(SH_IND_IDENTIFY, 600));
    CHECK(sh_ind_lit(SH_IND_IDENTIFY, 800));
    CHECK(sh_ind_lit(SH_IND_IDENTIFY, 1099));
    CHECK(!sh_ind_lit(SH_IND_IDENTIFY, 1100));                  /* the second group */
    CHECK(!sh_ind_lit(SH_IND_IDENTIFY, 2200));                  /* the third */
    CHECK(sh_ind_lit(SH_IND_IDENTIFY, 2800));
    CHECK(sh_ind_lit(SH_IND_IDENTIFY, 3000));                   /* over: what was shown before */
    CHECK(sh_ind_lit(SH_IND_IDENTIFY, 100000));
    CHECK_EQ(sh_ind_level(SH_IND_IDENTIFY, 0), 0);
    CHECK_EQ(sh_ind_level(SH_IND_IDENTIFY, 200), 100);
    /* over everything (also the alarms and the breathing) but not over the firmware update */
    CHECK(sh_ind_identify_ok(SH_IND_STEADY));
    CHECK(sh_ind_identify_ok(SH_IND_BREATHE));
    CHECK(sh_ind_identify_ok(SH_IND_HEARTBEAT));
    CHECK(sh_ind_identify_ok(SH_IND_WINK));
    CHECK(sh_ind_identify_ok(SH_IND_FAST));
    CHECK(sh_ind_identify_ok(SH_IND_DOUBLE));
    CHECK(!sh_ind_identify_ok(SH_IND_BLINK));
    /* sh_ind_update() never gives it: it is played once by sh_led */
    sh_ind_in_t in;
    memset(&in, 0, sizeof(in));
    in.wifi_connected = true;
    in.pair_window = true;
    CHECK(ind(&in).pattern != SH_IND_IDENTIFY);
}

/* ---- the web interface ------------------------------------------------------------------ */

#define HAS(s, text)                                                                \
    do {                                                                            \
        CHECK(sh_web_status_json(s_json, sizeof(s_json), (s)) > 0);                 \
        CHECK(strstr(s_json, (text)) != NULL);                                      \
    } while (0)

static char s_file[256 * 1024];

static const char *file(const char *path)
{
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    size_t n = 0;
    if (f) {
        n = fread(s_file, 1, sizeof(s_file) - 1, f);
        fclose(f);
    }
    s_file[n] = '\0';
    return s_file;
}

static void test_web(void)
{
    sh_web_status_t s = {
        .t_col = 523, .t_tank = 418,
        .cfg = { .settings = S, .frost = true,
                 .inst = { .v = { SH_FLUID_WATER, 0, 15, 120, SH_COL_FLAT, 20, 150, 20 } } },
        .col_protect = 1100, .name = "Solar-810840", .version = "0.1.0", .serial = "E8F60A810840",
        .ssid = "Home", .ip = "192.168.1.7", .rssi = -41, .host = "Solar-810840", .ap_ssid = "Solar-810840",
        .clock = { .trust = SH_CLOCK_UNSET, .min = -1, .tz = "UTC", .peak = SH_PEAK_NONE },
        .cool_until = -1, .sunrise = -1, .sunset = -1, .e8_normal = INT16_MIN,
    };
    /* the function is there: its elements are not grey */
    HAS(&s, "\"homekit\",\"keylock\"]");
    /* the setting, paired, running, the pairing window */
    HAS(&s, "\"hap\":{\"on\":0,\"paired\":0,\"run\":0,\"win\":0,\"f\":0},\"lock\":0,");
    s.hap_on = true;
    HAS(&s, "\"hap\":{\"on\":1,\"paired\":0,\"run\":0,\"win\":0,\"f\":0},\"lock\":0,");     /* waiting for the router */
    s.hap_running = true;
    s.hap_window = true;
    HAS(&s, "\"hap\":{\"on\":1,\"paired\":0,\"run\":1,\"win\":1,\"f\":0},\"lock\":0,");
    s.hap_paired = true;
    s.hap_window = false;
    HAS(&s, "\"hap\":{\"on\":1,\"paired\":1,\"run\":1,\"win\":0,\"f\":0},\"lock\":0,");
    s.key_lock = true;                                          /* the keyboard lock */
    HAS(&s, "\"f\":0},\"lock\":1,");
    s.key_lock = false;
    /* the whole status still fits its buffer */
    CHECK(sh_web_status_json(s_json, sizeof(s_json), &s) < SH_WEB_STATUS_MAX - 256);

    /* "Reset -> HomeKit pairings": with the access code */
    sh_web_reset_t what = SH_WEB_RESET_WIFI;
    CHECK_EQ(sh_web_reset("what=homekit&code=" CODE, CODE, &what), SH_WEB_OK);
    CHECK_EQ(what, SH_WEB_RESET_HOMEKIT);
    CHECK_EQ(sh_web_reset("what=homekit", CODE, &what), SH_WEB_DENIED);
    CHECK_EQ(sh_web_reset("what=homekit&code=000000", CODE, &what), SH_WEB_DENIED);

    /* the page: "Paired", the switch, the reset — connected, none of them without its function */
    const char *page = file("../main/web/index.html");
    CHECK(strstr(page, "id=\"sw-hap\"") != NULL);
    CHECK(strstr(page, "id=\"hap-v\"") != NULL);
    CHECK(strstr(page, "id=\"hap-note\"") != NULL);
    CHECK(strstr(page, "data-r=\"homekit\"") != NULL);
    CHECK(strstr(page, "'/api/hap'") != NULL);
    CHECK(strstr(page, "Първо премахнете устройството от Apple Home") != NULL);
    CHECK(strstr(page, "First remove the device from Apple Home") != NULL);
    CHECK(strstr(page, "q_homekit:") != NULL);
    CHECK(strstr(page, "id=\"sw-lock\"") != NULL);                          /* "Keyboard: locked / unlocked" */
    CHECK(strstr(page, "id=\"lock-v\"") != NULL);
    CHECK(strstr(page, "'/api/lock'") != NULL);
    CHECK(strstr(page, "Клавиатура: заключена") != NULL);
    /* °F: the page converts what it shows and what is entered (the data stay °C) */
    CHECK(strstr(page, "S.hap.f") != NULL);
    CHECK(strstr(page, "data-k=\"tbo\" data-u=\"t\"") != NULL);
    CHECK(strstr(page, "data-k=\"d_on\" data-u=\"d\"") != NULL);
    CHECK(strstr(page, "data-k=\"col_max\" data-u=\"t\"") != NULL);
    CHECK(strstr(page, "data-k=\"press\" data-u") == NULL);                 /* bar: not a temperature */
    CHECK(strstr(page, "data-k=\"col_area\" data-u") == NULL);
    CHECK(strstr(page, "<button class=\"row\" data-f=\"homekit\"><span") == NULL);    /* the old dead row */
    /* the server: the setting and the reset reach sh_hap; the pages can move to the server of HAP */
    const char *srv = file("../main/sh_web.c");
    CHECK(strstr(srv, "\"/api/hap\"") != NULL);
    CHECK(strstr(srv, "\"/api/lock\"") != NULL);
    CHECK(strstr(srv, "sh_hap_set_lock(") != NULL);
    CHECK(strstr(srv, "sh_hap_set_enabled(") != NULL);
    CHECK(strstr(srv, "sh_hap_reset_pairings(") != NULL);
    CHECK(strstr(srv, "void sh_web_attach(httpd_handle_t") != NULL);
    /* HAP is never stopped */
    const char *hap = file("../main/sh_hap.c");
    CHECK(strstr(hap, "hap_start()") != NULL);
    CHECK(strstr(hap, "hap_stop(") == NULL);
}

int main(void)
{
    test_active_vacation();
    test_tmax();
    test_current_state();
    test_temperatures();
    test_units();
    test_display_units();
    test_lock();
    test_status_fault();
    test_notifications();
    test_start();
    test_setting();
    test_pairing_window();
    test_identify();
    test_web();
    return test_summary("test_hap");
}
