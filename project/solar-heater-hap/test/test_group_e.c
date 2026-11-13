/* Host test of group E (doc/tasks/step7-network-ota.md 3, 5.2): the network mode CP / AP (sh_net) with
 * the timers of the access point, the return to CP with / without a stored network, AP refused while
 * connected to the router; the name, the access code and the serial number from the base MAC; the menu
 * nEt and the code on the display, "AP" and "FUp" after the cycle of the normal screen (sh_menu); the
 * LED in AP and during OTA (sh_ind); the firmware update — the automatic check 1 min after the restart,
 * "FUp" and its confirmation, the wait while the pump runs or a protection is active (sh_fw); the web
 * interface — the mode only shown, the name, the update and its texts in AP / without the server
 * (sh_web_api). Times: s (sh_net, sh_fw), ms (sh_menu, sh_ind). */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_net.h"
#include "sh_fw.h"
#include "sh_menu.h"
#include "sh_ind.h"
#include "sh_web_api.h"

/* ---- the network mode ------------------------------------------------------------------- */

static void test_net_start_and_select(void)
{
    sh_net_t n;
    sh_net_init(&n);
    CHECK_EQ(n.mode, SH_NET_CP);                                    /* always CP at the start */
    CHECK_EQ(sh_net_select(&n, SH_NET_CP, false, false, 10), SH_NET_ACT_NONE);

    /* connected to the router: AP refused, nothing changes */
    CHECK(!sh_net_ap_allowed(true));
    CHECK_EQ(sh_net_select(&n, SH_NET_AP, true, false, 10), SH_NET_ACT_NONE);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK(!n.ap_pending);

    /* not connected (no network, the router does not answer, the radio off): allowed */
    CHECK(sh_net_ap_allowed(false));
    CHECK_EQ(sh_net_select(&n, SH_NET_AP, false, false, 10), SH_NET_ACT_START_AP);
    CHECK_EQ(n.mode, SH_NET_AP);
    CHECK_EQ(sh_net_select(&n, SH_NET_AP, false, false, 11), SH_NET_ACT_NONE);  /* already */

    /* CP from AP: the router / the portal */
    CHECK_EQ(sh_net_select(&n, SH_NET_CP, false, false, 20), SH_NET_ACT_START_CP);
    CHECK_EQ(n.mode, SH_NET_CP);
}

static void test_net_ap_timers(void)
{
    sh_net_t n;
    sh_net_init(&n);
    const uint32_t t0 = 1000;
    sh_net_select(&n, SH_NET_AP, false, false, t0);

    /* 15 min without a client: ±1 s */
    CHECK_EQ(sh_net_ap_left(&n, t0), 900);
    CHECK_EQ(sh_net_tick(&n, t0 + 899, true), SH_NET_ACT_NONE);
    CHECK_EQ(n.mode, SH_NET_AP);
    CHECK_EQ(sh_net_ap_left(&n, t0 + 899), 1);
    CHECK_EQ(sh_net_tick(&n, t0 + 900, true), SH_NET_ACT_START_CP);   /* with a network: the router */
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK(!n.radio_off);

    /* a client: no time-out while it is there; 15 min after the last one left */
    sh_net_select(&n, SH_NET_AP, false, false, t0);
    sh_net_client(&n, true, t0 + 100);
    sh_net_client(&n, true, t0 + 200);
    CHECK_EQ(sh_net_ap_left(&n, t0 + 5000), 0);
    CHECK_EQ(sh_net_tick(&n, t0 + 5000, true), SH_NET_ACT_NONE);
    sh_net_client(&n, false, t0 + 6000);                            /* one left, one stays */
    CHECK_EQ(sh_net_tick(&n, t0 + 7000, true), SH_NET_ACT_NONE);
    sh_net_client(&n, false, t0 + 8000);                            /* the last one left */
    CHECK_EQ(sh_net_ap_left(&n, t0 + 8000), 900);
    CHECK_EQ(sh_net_tick(&n, t0 + 8899, false), SH_NET_ACT_NONE);
    CHECK_EQ(sh_net_tick(&n, t0 + 8900, false), SH_NET_ACT_RADIO_OFF);  /* no network: radio off */
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK(n.radio_off);

    /* the radio off: CP from the menu starts the portal again */
    CHECK_EQ(sh_net_select(&n, SH_NET_CP, false, false, t0 + 9000), SH_NET_ACT_START_CP);
    CHECK(!n.radio_off);

    /* captive-wifi ended it first (AP_TIMEOUT): the same, only once */
    sh_net_select(&n, SH_NET_AP, false, false, t0);
    CHECK_EQ(sh_net_ap_timeout(&n, true), SH_NET_ACT_START_CP);
    CHECK_EQ(sh_net_ap_timeout(&n, true), SH_NET_ACT_NONE);
    CHECK_EQ(sh_net_tick(&n, t0 + 2000, true), SH_NET_ACT_NONE);
    sh_net_select(&n, SH_NET_AP, false, false, t0);
    CHECK_EQ(sh_net_ap_timeout(&n, false), SH_NET_ACT_RADIO_OFF);

    /* the portal closed without a client (no network): the radio is off until a choice */
    sh_net_init(&n);
    sh_net_portal_timeout(&n);
    CHECK(n.radio_off);
    CHECK_EQ(sh_net_select(&n, SH_NET_CP, false, false, 10), SH_NET_ACT_START_CP);
}

static void test_net_deferred_by_update(void)
{
    sh_net_t n;
    sh_net_init(&n);
    CHECK_EQ(sh_net_select(&n, SH_NET_AP, false, true, 10), SH_NET_ACT_NONE);   /* update busy */
    CHECK(n.ap_pending);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK_EQ(sh_net_fw_done(&n, 50), SH_NET_ACT_START_AP);          /* after the update */
    CHECK_EQ(n.mode, SH_NET_AP);
    CHECK(!n.ap_pending);
    CHECK_EQ(sh_net_ap_left(&n, 50), 900);                          /* the 15 min count from now */
    CHECK_EQ(sh_net_fw_done(&n, 60), SH_NET_ACT_NONE);

    /* CP cancels a deferred AP */
    sh_net_init(&n);
    sh_net_select(&n, SH_NET_AP, false, true, 10);
    CHECK_EQ(sh_net_select(&n, SH_NET_CP, false, true, 11), SH_NET_ACT_NONE);
    CHECK(!n.ap_pending);
    CHECK_EQ(sh_net_fw_done(&n, 50), SH_NET_ACT_NONE);
}

static void test_name_code_serial(void)
{
    const uint8_t mac[6] = { 0xE8, 0xF6, 0x0A, 0x81, 0x08, 0x40 };   /* the base MAC */
    char name[SH_NET_NAME_MAX], code[8], serial[13];
    sh_net_name(name, sizeof(name), "Solar", mac);
    CHECK(strcmp(name, "Solar-810840") == 0);                       /* network, DHCP, mDNS */
    sh_net_code(code, sizeof(code), name);
    CHECK(strcmp(code, "810840") == 0);
    sh_net_serial(serial, sizeof(serial), mac);
    CHECK(strcmp(serial, "E8F60A810840") == 0);
    const uint8_t hex[6] = { 0, 0, 0, 0xAB, 0xCD, 0xEF };
    sh_net_name(name, sizeof(name), "Solar", hex);
    CHECK(strcmp(name, "Solar-abcdef") == 0);                       /* lower case, as captive-wifi */
    sh_net_code(code, sizeof(code), name);
    CHECK(strcmp(code, "abcdef") == 0);
    /* the code is taken in lower and upper case */
    CHECK(sh_web_code_ok("code=abcdef", code));
    CHECK(sh_web_code_ok("code=ABCDEF", code));
    CHECK(sh_web_code_ok("code=AbCdEf", code));
    CHECK(!sh_web_code_ok("code=abcde0", code));
}

/* ---- the menu and the display ----------------------------------------------------------- */

static const sh_settings_t S = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };

static sh_menu_act_t key(sh_menu_t *m, sh_key_t k, uint32_t *now, uint32_t dt, const sh_menu_ctx_t *ctx)
{
    *now += dt;
    return sh_menu_key(m, k, *now, ctx, &S);
}

/* the list at nEt */
static void to_net(sh_menu_t *m, uint32_t *now, const sh_menu_ctx_t *ctx)
{
    *m = (sh_menu_t){ 0 };
    key(m, SH_KEY_MENU, now, 0, ctx);
    for (int i = 0; i < SH_MENU_NET; i++) {
        key(m, SH_KEY_PLUS, now, 100, ctx);
    }
    CHECK_EQ(m->pos, SH_MENU_NET);
}

static void test_menu_net(void)
{
    sh_menu_t m;
    uint32_t now = 1000;
    sh_menu_ctx_t ctx = { 0 };
    sh_menu_view_t v;

    /* the value shows the mode in use; CP <-> AP without wrap */
    to_net(&m, &now, &ctx);
    CHECK_EQ(key(&m, SH_KEY_MENU, &now, 100, &ctx), SH_MENU_ACT_NONE);
    CHECK_EQ(m.mode, SH_MENU_EDIT);
    CHECK_EQ(m.value, 0);                                           /* CP */
    key(&m, SH_KEY_MINUS, &now, 100, &ctx);
    CHECK_EQ(m.value, 0);
    key(&m, SH_KEY_PLUS, &now, 100, &ctx);
    CHECK_EQ(m.value, 1);                                           /* AP */
    key(&m, SH_KEY_PLUS, &now, 100, &ctx);
    CHECK_EQ(m.value, 1);

    /* AP confirmed without a connection: the action, then the code in two parts of 2 s */
    CHECK_EQ(key(&m, SH_KEY_MENU, &now, 100, &ctx), SH_MENU_ACT_NET);
    CHECK_EQ(m.value, 1);
    const uint32_t t0 = now;
    sh_menu_update(&m, t0 + 1999);
    sh_menu_view(&m, t0 + 1999, &v);
    CHECK_EQ(v.mode, SH_MENU_CODE);
    CHECK_EQ(v.code_part, 0);                                       /* "810" */
    sh_menu_update(&m, t0 + 2000);
    sh_menu_view(&m, t0 + 2000, &v);
    CHECK_EQ(v.mode, SH_MENU_CODE);
    CHECK_EQ(v.code_part, 1);                                       /* "840" */
    now = t0 + 3000;
    CHECK_EQ(key(&m, SH_KEY_PLUS, &now, 0, &ctx), SH_MENU_ACT_NONE);   /* keys ignored meanwhile */
    sh_menu_update(&m, t0 + 3999);
    CHECK_EQ(m.mode, SH_MENU_CODE);
    sh_menu_update(&m, t0 + 4000);
    CHECK_EQ(m.mode, SH_MENU_NORMAL);                               /* then the normal screen */

    /* AP confirmed while connected to the router: refused — "CP" steady 1 s, then the list */
    ctx.net_connected = true;
    now = 10000;
    to_net(&m, &now, &ctx);
    key(&m, SH_KEY_MENU, &now, 100, &ctx);
    key(&m, SH_KEY_PLUS, &now, 100, &ctx);
    CHECK_EQ(m.value, 1);
    CHECK_EQ(key(&m, SH_KEY_MENU, &now, 100, &ctx), SH_MENU_ACT_NONE);
    sh_menu_view(&m, now, &v);
    CHECK_EQ(v.mode, SH_MENU_SAVED);
    CHECK_EQ(v.pos, SH_MENU_NET);
    CHECK_EQ(v.value, 0);                                           /* "CP" */
    CHECK(v.value_visible);                                         /* steady */
    sh_menu_update(&m, now + 999);
    CHECK_EQ(m.mode, SH_MENU_SAVED);
    sh_menu_update(&m, now + 1000);
    CHECK_EQ(m.mode, SH_MENU_LIST);
    CHECK_EQ(m.pos, SH_MENU_NET);

    /* in AP: shows AP; CP confirmed -> the action, steady 1 s, the list */
    ctx = (sh_menu_ctx_t){ .net_ap = true };
    now = 20000;
    to_net(&m, &now, &ctx);
    key(&m, SH_KEY_MENU, &now, 100, &ctx);
    CHECK_EQ(m.value, 1);
    key(&m, SH_KEY_MINUS, &now, 100, &ctx);
    CHECK_EQ(key(&m, SH_KEY_MENU, &now, 100, &ctx), SH_MENU_ACT_NET);
    CHECK_EQ(m.value, 0);
    CHECK_EQ(m.mode, SH_MENU_SAVED);
    /* the same mode confirmed: nothing */
    now = 30000;
    to_net(&m, &now, &ctx);
    key(&m, SH_KEY_MENU, &now, 100, &ctx);
    CHECK_EQ(key(&m, SH_KEY_MENU, &now, 100, &ctx), SH_MENU_ACT_NONE);
    CHECK_EQ(m.mode, SH_MENU_CODE);                                 /* AP shows the code again */
}

static void test_menu_fup(void)
{
    sh_menu_t m = { 0 };
    uint32_t now = 1000;
    sh_menu_ctx_t ctx = { .fw_prompt = true, .test_running = true };
    /* "FUp" asks: "menu" confirms (before stopping the test or opening the list) */
    CHECK_EQ(key(&m, SH_KEY_MENU, &now, 0, &ctx), SH_MENU_ACT_FW_CONFIRM);
    CHECK_EQ(m.mode, SH_MENU_NORMAL);
    ctx.fw_prompt = false;
    CHECK_EQ(key(&m, SH_KEY_MENU, &now, 0, &ctx), SH_MENU_ACT_STOP);
    ctx.test_running = false;
    CHECK_EQ(key(&m, SH_KEY_MENU, &now, 0, &ctx), SH_MENU_ACT_NONE);
    CHECK_EQ(m.mode, SH_MENU_LIST);
}

static void test_display_tail(void)
{
    uint8_t code = 0;
    /* nothing: the cycle starts again at once */
    CHECK_EQ(sh_menu_tail(0, false, false, false, 0, &code), SH_MENU_TAIL_END);
    /* AP: "AP" 1 s after the cycle */
    CHECK_EQ(sh_menu_tail(0, false, true, false, 0, &code), SH_MENU_TAIL_AP);
    CHECK_EQ(sh_menu_tail(0, false, true, false, 999, &code), SH_MENU_TAIL_AP);
    CHECK_EQ(sh_menu_tail(0, false, true, false, 1000, &code), SH_MENU_TAIL_END);
    /* the codes first, then HoL, AP, FUp — 1 s each */
    const uint8_t al = SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_HOT);
    CHECK_EQ(sh_menu_tail(al, true, true, true, 0, &code), SH_MENU_TAIL_CODE);
    CHECK_EQ(code, SH_ALARM_E1);
    CHECK_EQ(sh_menu_tail(al, true, true, true, 1000, &code), SH_MENU_TAIL_CODE);
    CHECK_EQ(code, SH_ALARM_HOT);
    CHECK_EQ(sh_menu_tail(al, true, true, true, 2000, &code), SH_MENU_TAIL_HOL);
    CHECK_EQ(sh_menu_tail(al, true, true, true, 3000, &code), SH_MENU_TAIL_AP);
    CHECK_EQ(sh_menu_tail(al, true, true, true, 4000, &code), SH_MENU_TAIL_FUP);
    CHECK_EQ(sh_menu_tail(al, true, true, true, 5000, &code), SH_MENU_TAIL_END);
    /* the update waits: "FUp" */
    CHECK_EQ(sh_menu_tail(0, false, false, true, 0, &code), SH_MENU_TAIL_FUP);
    CHECK_EQ(sh_menu_tail(0, false, false, true, 1000, &code), SH_MENU_TAIL_END);
}

/* ---- the LED ---------------------------------------------------------------------------- */

static void test_ind_ap_ota(void)
{
    const sh_ind_params_t p = { .cold = 200, .hot = 900 };
    sh_ind_state_t st = { 0 };
    sh_ind_in_t in;
    memset(&in, 0, sizeof(in));
    in.t_col = 500;

    /* AP: the colour of the process, lit with a dark 80 ms every 2 s (the mirror of "CP, no Wi-Fi") */
    in.ap = true;
    sh_ind_out_t o = sh_ind_update(&st, &p, &in);
    CHECK_EQ(o.color, SH_COLOR_BLUE);
    CHECK_EQ(o.pattern, SH_IND_AP);
    in.pump_on = true;
    CHECK_EQ(sh_ind_update(&st, &p, &in).color, SH_COLOR_GREEN);
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_AP);
    CHECK(!sh_ind_lit(SH_IND_AP, 0));
    CHECK(!sh_ind_lit(SH_IND_AP, 79));
    CHECK(sh_ind_lit(SH_IND_AP, 80));
    CHECK(sh_ind_lit(SH_IND_AP, 1999));
    CHECK(!sh_ind_lit(SH_IND_AP, 2000));
    CHECK(sh_ind_lit(SH_IND_AP, 2080));
    for (uint32_t ms = 0; ms < 4000; ms += 10) {
        CHECK(sh_ind_lit(SH_IND_AP, ms) == !sh_ind_lit(SH_IND_HEARTBEAT, ms));
    }

    /* the alarm patterns come first */
    in.alarms = SH_ALARM_BIT(SH_ALARM_E1);
    o = sh_ind_update(&st, &p, &in);
    CHECK_EQ(o.color, SH_COLOR_RED);
    CHECK_EQ(o.pattern, SH_IND_DOUBLE);
    in.alarms = SH_ALARM_BIT(SH_ALARM_E4);
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_FAST);

    /* OTA before everything: white, 500 ms on / 500 ms off */
    in.ota = true;
    o = sh_ind_update(&st, &p, &in);
    CHECK_EQ(o.color, SH_COLOR_WHITE);
    CHECK_EQ(o.pattern, SH_IND_OTA);
    CHECK(sh_ind_lit(SH_IND_OTA, 0));
    CHECK(sh_ind_lit(SH_IND_OTA, 499));
    CHECK(!sh_ind_lit(SH_IND_OTA, 500));
    CHECK(!sh_ind_lit(SH_IND_OTA, 999));
    CHECK(sh_ind_lit(SH_IND_OTA, 1000));

    /* the end of AP: back to steady / heartbeat */
    memset(&in, 0, sizeof(in));
    in.t_col = 500;
    in.wifi_connected = true;
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_STEADY);
    in.wifi_connected = false;
    CHECK_EQ(sh_ind_update(&st, &p, &in).pattern, SH_IND_HEARTBEAT);
}

/* ---- the firmware update ---------------------------------------------------------------- */

static void test_fw_auto_check(void)
{
    sh_fw_t f = { 0 };
    /* connected early: at 1 min after the restart, once */
    sh_fw_link(&f, true, 8);
    CHECK(!sh_fw_auto_due(&f, 59));
    CHECK(sh_fw_auto_due(&f, 60));
    CHECK(!sh_fw_auto_due(&f, 61));

    /* no connection at 1 min: 1 min after the connection */
    f = (sh_fw_t){ 0 };
    CHECK(!sh_fw_auto_due(&f, 60));
    CHECK(!sh_fw_auto_due(&f, 300));
    sh_fw_link(&f, true, 400);
    CHECK(!sh_fw_auto_due(&f, 459));
    CHECK(sh_fw_auto_due(&f, 460));
    CHECK(!sh_fw_auto_due(&f, 1000));

    /* the connection lost before the time: counted from the next one */
    f = (sh_fw_t){ 0 };
    sh_fw_link(&f, true, 100);
    sh_fw_link(&f, false, 130);
    CHECK(!sh_fw_auto_due(&f, 160));
    sh_fw_link(&f, true, 500);
    CHECK(!sh_fw_auto_due(&f, 559));
    CHECK(sh_fw_auto_due(&f, 560));

    /* a failed automatic check: only the state (the log), no prompt */
    CHECK(sh_fw_check_start(&f, true));
    CHECK(sh_fw_busy(&f));
    sh_fw_check_done(&f, SH_FW_RES_ERROR, NULL, 561);
    CHECK_EQ(f.state, SH_FW_NO_SERVER);
    CHECK(!sh_fw_prompt(&f, 561));
    CHECK(!sh_fw_busy(&f));
    /* up to date */
    CHECK(sh_fw_check_start(&f, false));
    sh_fw_check_done(&f, SH_FW_RES_CURRENT, "0.1.0", 600);
    CHECK_EQ(f.state, SH_FW_CURRENT);
}

static void test_fw_prompt_confirm(void)
{
    sh_fw_t f = { 0 };
    /* the automatic check finds a newer version: "FUp" for 5 s */
    CHECK(sh_fw_check_start(&f, true));
    CHECK(!sh_fw_check_start(&f, false));                           /* one operation at a time */
    sh_fw_check_done(&f, SH_FW_RES_AVAILABLE, "0.2.0", 100);
    CHECK_EQ(f.state, SH_FW_AVAILABLE);
    CHECK(strcmp(f.ver, "0.2.0") == 0);
    CHECK(sh_fw_prompt(&f, 100));
    CHECK(sh_fw_prompt(&f, 104));
    CHECK(!sh_fw_prompt(&f, 105));
    /* no confirmation in time: nothing is installed */
    CHECK(!sh_fw_confirm(&f, 105, true));
    CHECK_EQ(f.state, SH_FW_AVAILABLE);
    CHECK(!sh_fw_step(&f, false, 0));

    /* confirmed with "menu" during the prompt */
    f = (sh_fw_t){ 0 };
    sh_fw_check_start(&f, true);
    sh_fw_check_done(&f, SH_FW_RES_AVAILABLE, "0.2.0", 100);
    CHECK(sh_fw_confirm(&f, 103, true));
    CHECK_EQ(f.state, SH_FW_WAITING);
    CHECK(!sh_fw_prompt(&f, 103));
    CHECK(sh_fw_busy(&f));

    /* a check by hand: no prompt; "Update" from the web interface confirms */
    f = (sh_fw_t){ 0 };
    sh_fw_check_start(&f, false);
    sh_fw_check_done(&f, SH_FW_RES_AVAILABLE, "0.2.0", 100);
    CHECK(!sh_fw_prompt(&f, 100));
    CHECK(!sh_fw_confirm(&f, 100, true));                           /* not from the menu */
    CHECK(sh_fw_confirm(&f, 100, false));
    CHECK_EQ(f.state, SH_FW_WAITING);
}

static void test_fw_wait_and_end(void)
{
    sh_fw_t f = { 0 };
    sh_fw_check_start(&f, false);
    sh_fw_check_done(&f, SH_FW_RES_AVAILABLE, "0.2.0", 100);
    sh_fw_confirm(&f, 100, false);

    /* waits while the pump runs or a protection is active (Hot, Fro, E4, E5) */
    CHECK(!sh_fw_step(&f, true, 0));
    const sh_alarm_t prot[] = { SH_ALARM_HOT, SH_ALARM_FRO, SH_ALARM_E4, SH_ALARM_E5 };
    for (unsigned i = 0; i < sizeof(prot) / sizeof(prot[0]); i++) {
        CHECK(!sh_fw_may_start(false, (uint8_t)SH_ALARM_BIT(prot[i])));
        CHECK(!sh_fw_step(&f, false, (uint8_t)SH_ALARM_BIT(prot[i])));
    }
    CHECK_EQ(f.state, SH_FW_WAITING);
    /* a sensor fault is no protection */
    CHECK(sh_fw_may_start(false, (uint8_t)SH_ALARM_BIT(SH_ALARM_E1)));
    /* the pump stopped, no protection: starts */
    CHECK(sh_fw_step(&f, false, 0));
    CHECK_EQ(f.state, SH_FW_UPDATING);
    CHECK(!sh_fw_step(&f, false, 0));
    sh_fw_progress(&f, 45);
    CHECK_EQ(f.pct, 45);
    /* the pump logic goes on meanwhile: the update is not stopped by it */
    sh_fw_progress(&f, 50);
    CHECK_EQ(f.state, SH_FW_UPDATING);

    /* failed: the old version stays, a new check is possible */
    sh_fw_update_done(&f, false);
    CHECK_EQ(f.state, SH_FW_FAILED);
    CHECK(!sh_fw_busy(&f));
    CHECK(sh_fw_check_start(&f, false));

    /* written: done (restart), nothing more */
    f = (sh_fw_t){ .state = SH_FW_UPDATING };
    sh_fw_update_done(&f, true);
    CHECK_EQ(f.state, SH_FW_DONE);
    CHECK_EQ(f.pct, 100);
    CHECK(!sh_fw_check_start(&f, false));
}

/* ---- the web interface ------------------------------------------------------------------ */

static sh_web_status_t status(void)
{
    const sh_web_status_t s = {
        .t_col = 523, .t_tank = 418,
        .cfg = { .settings = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } },
                 .frost = true, .inst = { .v = { SH_FLUID_WATER, 0, 15, 120, SH_COL_FLAT, 20, 150, 20 } } },
        .col_protect = 1100,
        .name = "Solar-810840", .version = "0.1.0", .serial = "E8F60A810840", .ssid = "Home",
        .ip = "192.168.1.7", .rssi = -41, .mdns = true,
        .clock = { .trust = SH_CLOCK_UNSET, .min = -1, .tz = "UTC", .peak = SH_PEAK_NONE },
        .cool_until = -1, .sunrise = -1, .sunset = -1, .e8_normal = INT16_MIN,
    };
    return s;
}

static char s_json[SH_WEB_STATUS_MAX];

static bool has(const sh_web_status_t *s, const char *text)
{
    const int len = sh_web_status_json(s_json, sizeof(s_json), s);
    CHECK(len > 0);
    return len > 0 && strstr(s_json, text) != NULL;
}

static void test_web_status(void)
{
    sh_web_status_t s = status();
    /* the functions of the step are there */
    CHECK(has(&s, "\"net_mode\""));
    CHECK(has(&s, "\"ota\""));
    /* CP with the router: the name (mDNS), the serial number */
    CHECK(has(&s, "\"sn\":\"E8F60A810840\""));
    CHECK(has(&s, "\"mode\":\"CP\",\"pend\":0,\"host\":\"Solar-810840\",\"mdns\":1,\"cl\":0,\"left\":null}"));
    /* AP: the own network, 192.168.4.1, the time left */
    s.net_ap = true;
    s.mdns = false;
    s.ssid = "Solar-810840";
    s.ip = "192.168.4.1";
    s.ap_left_s = 754;
    CHECK(has(&s, "\"ssid\":\"Solar-810840\",\"ip\":\"192.168.4.1\""));
    CHECK(has(&s, "\"mode\":\"AP\",\"pend\":0,\"host\":\"Solar-810840\",\"mdns\":0,\"cl\":0,\"left\":754}"));
    s.ap_clients = 1;
    CHECK(has(&s, "\"cl\":1,\"left\":null}"));
    /* AP deferred by the update */
    s = status();
    s.net_pending = true;
    CHECK(has(&s, "\"mode\":\"CP\",\"pend\":1"));

    /* the update: the states and the texts' keys of the page */
    s = status();
    CHECK(has(&s, "\"ota\":{\"st\":\"idle\",\"pct\":0,\"ver\":\"\"}"));
    s.fw = SH_FW_CURRENT;
    CHECK(has(&s, "\"ota\":{\"st\":\"current\""));                 /* "Up to date" */
    s.fw = SH_FW_AVAILABLE;
    s.fw_ver = "0.2.0";
    CHECK(has(&s, "\"ota\":{\"st\":\"avail\",\"pct\":0,\"ver\":\"0.2.0\"}"));   /* "A newer version 0.2.0" */
    s.fw = SH_FW_NO_SERVER;
    CHECK(has(&s, "\"st\":\"noserver\""));                          /* "No connection to the update server" */
    s.fw = SH_FW_WAITING;
    CHECK(has(&s, "\"st\":\"wait\""));
    s.fw = SH_FW_UPDATING;
    s.fw_pct = 45;
    CHECK(has(&s, "\"ota\":{\"st\":\"run\",\"pct\":45,"));
    s.fw = SH_FW_FAILED;
    CHECK(has(&s, "\"st\":\"fail\""));
}

static void test_web_requests(void)
{
    sh_web_ota_t cmd;
    const char *code = "810840";
    /* with the code */
    CHECK_EQ(sh_web_ota("cmd=check", code, false, SH_FW_IDLE, &cmd), SH_WEB_DENIED);
    CHECK_EQ(sh_web_ota("cmd=check&code=000000", code, false, SH_FW_IDLE, &cmd), SH_WEB_DENIED);
    CHECK_EQ(sh_web_ota("cmd=check&code=810840", code, false, SH_FW_IDLE, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_OTA_CHECK);
    CHECK_EQ(sh_web_ota("cmd=x&code=810840", code, false, SH_FW_IDLE, &cmd), SH_WEB_BAD);
    /* AP: no internet — refused */
    CHECK_EQ(sh_web_ota("cmd=check&code=810840", code, true, SH_FW_IDLE, &cmd), SH_WEB_BAD);
    /* "Update" only with a newer version found */
    CHECK_EQ(sh_web_ota("cmd=update&code=810840", code, false, SH_FW_CURRENT, &cmd), SH_WEB_BAD);
    CHECK_EQ(sh_web_ota("cmd=update&code=810840", code, false, SH_FW_AVAILABLE, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_OTA_UPDATE);
    CHECK_EQ(sh_web_ota("cmd=update&code=810840", code, true, SH_FW_AVAILABLE, &cmd), SH_WEB_BAD);
    /* not while busy */
    CHECK_EQ(sh_web_ota("cmd=check&code=810840", code, false, SH_FW_UPDATING, &cmd), SH_WEB_BAD);
    CHECK_EQ(sh_web_ota("cmd=check&code=810840", code, false, SH_FW_WAITING, &cmd), SH_WEB_BAD);
    CHECK_EQ(sh_web_ota("cmd=check&code=810840", code, false, SH_FW_NO_SERVER, &cmd), SH_WEB_OK);

    /* the network mode is only shown: no request changes it */
    sh_web_config_t c = {
        .settings = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } },
        .inst = { .v = { SH_FLUID_WATER, 0, 15, 120, SH_COL_FLAT, 20, 150, 20 } },
    };
    unsigned ch = 0;
    CHECK_EQ(sh_web_set("net=1&code=810840", code, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(sh_web_set("mode=1&code=810840", code, &c, &ch), SH_WEB_BAD);
}

int main(void)
{
    test_net_start_and_select();
    test_net_ap_timers();
    test_net_deferred_by_update();
    test_name_code_serial();
    test_menu_net();
    test_menu_fup();
    test_display_tail();
    test_ind_ap_ota();
    test_fw_auto_check();
    test_fw_prompt_confirm();
    test_fw_wait_and_end();
    test_web_status();
    test_web_requests();
    return test_summary("test_group_e");
}
