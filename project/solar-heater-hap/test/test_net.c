/* Host test of group E (doc/tasks/step7-network-ota.md): the network mode CP / AP and its times
 * (main/sh_net_rules.c), the menu position nEt with the access code and the labels of the display
 * (main/sh_menu.c), the indication of the access point and of the update (main/sh_ind.c), the
 * firmware update rules (main/sh_ota.c) and what the web interface gets and sends
 * (main/sh_web_api.c). Times of the network in s, of the menu, the LED and the update in ms. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_ind.h"
#include "sh_menu.h"
#include "sh_net_rules.h"
#include "sh_ota.h"
#include "sh_web_api.h"

#define BIT(a)  SH_ALARM_BIT(a)
#define CODE    "810841"

/* ---- the menu: nEt, the access code, FUp ------------------------------------------------ */

static const sh_settings_t S = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };

typedef struct {
    sh_menu_t m;
    uint32_t now;
    sh_menu_ctx_t ctx;
} ui_t;

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

/* in the list at nEt */
static ui_t at_net(bool ap)
{
    ui_t u = { .now = 1000, .ctx = { .net_ap = ap } };
    key(&u, SH_KEY_MENU, 0);
    key(&u, SH_KEY_MINUS, 100);                                 /* SSn -> End */
    key(&u, SH_KEY_MINUS, 100);                                 /* End -> nEt */
    return u;
}

static void test_menu_net(void)
{
    /* nEt is before End: SSn, HoL, tbo, C.Lo, d.on, d.oF, tSt, nEt, End */
    CHECK_EQ(SH_MENU_NET, SH_MENU_TST + 1);
    CHECK_EQ(SH_MENU_END, SH_MENU_NET + 1);
    CHECK_EQ(SH_MENU_ITEMS, 9);
    ui_t u = at_net(false);
    CHECK_EQ(u.m.pos, SH_MENU_NET);
    CHECK_EQ(u.m.mode, SH_MENU_LIST);

    /* the value is the current mode, blinking; "+" AP, "-" CP, no wrap */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
    CHECK_EQ(view(&u, 0).value, 0);                             /* CP */
    CHECK(view(&u, 0).value_visible);
    CHECK(!view(&u, 500).value_visible);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(u.m.value, 0);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(u.m.value, 1);                                     /* AP */
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(u.m.value, 1);
    key(&u, SH_KEY_PLUS_REPEAT, 100);
    CHECK_EQ(u.m.value, 1);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(u.m.value, 0);
    key(&u, SH_KEY_PLUS, 100);

    /* AP confirmed: the choice goes out, the code in two parts of 2 s, then the list at nEt */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NET);
    CHECK_EQ(u.m.value, 1);
    sh_menu_view_t v = view(&u, 0);
    CHECK_EQ(v.mode, SH_MENU_SAVED);
    CHECK_EQ(v.code_part, 1);                                   /* "810" */
    CHECK(v.value_visible);
    CHECK_EQ(view(&u, 1999).code_part, 1);
    CHECK_EQ(view(&u, 1).code_part, 2);                         /* "841" */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);      /* keys are ignored meanwhile */
    CHECK_EQ(key(&u, SH_KEY_PLUS, 100), SH_MENU_ACT_NONE);
    v = view(&u, 1799);
    CHECK_EQ(v.mode, SH_MENU_SAVED);
    CHECK_EQ(v.code_part, 2);
    v = view(&u, 1);                                            /* 4 s after the confirmation */
    CHECK_EQ(v.mode, SH_MENU_LIST);
    CHECK_EQ(v.pos, SH_MENU_NET);
    CHECK_EQ(v.code_part, 0);

    /* in AP the value starts at AP; CP confirmed: "CP" steady for 1 s, no code */
    u = at_net(true);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(view(&u, 0).value, 1);
    key(&u, SH_KEY_MINUS, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NET);
    CHECK_EQ(u.m.value, 0);
    v = view(&u, 999);
    CHECK_EQ(v.mode, SH_MENU_SAVED);
    CHECK_EQ(v.code_part, 0);
    CHECK_EQ(view(&u, 1).mode, SH_MENU_LIST);

    /* the same mode again is passed on too (CP switches a radio that is off on again; AP shows the code) */
    u = at_net(false);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NET);
    CHECK_EQ(u.m.value, 0);
    u = at_net(true);
    key(&u, SH_KEY_MENU, 100);
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NET);
    CHECK_EQ(view(&u, 0).code_part, 1);

    /* not confirmed within 10 s: nothing is chosen */
    u = at_net(false);
    key(&u, SH_KEY_MENU, 100);
    key(&u, SH_KEY_PLUS, 100);
    CHECK_EQ(view(&u, 10000).mode, SH_MENU_NORMAL);
    CHECK_EQ(key(&u, SH_KEY_MENU, 0), SH_MENU_ACT_NONE);        /* enters the list again */

    /* the other positions are not disturbed: tSt is still a setting */
    CHECK_EQ(sh_menu_setting(SH_MENU_TST, false), SH_SET_TEST_S);
    CHECK_EQ(sh_menu_setting(SH_MENU_TBO, true), SH_SET_TANK_MAX);
}

static void test_menu_fup(void)
{
    /* "FUp" offered: a short "menu" on the normal screen confirms the update, nothing else */
    ui_t u = { .now = 1000, .ctx = { .fup_offer = true } };
    CHECK_EQ(key(&u, SH_KEY_MENU, 0), SH_MENU_ACT_FUP);
    CHECK_EQ(u.m.mode, SH_MENU_NORMAL);
    u.ctx.test_running = true;                                  /* also during the pump test */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_FUP);
    u.ctx.fup_offer = false;                                    /* the offer is over: as before */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_STOP);
    u.ctx.test_running = false;
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_LIST);
    u.ctx.fup_offer = true;                                     /* in the menu: "menu" is the menu's */
    CHECK_EQ(key(&u, SH_KEY_MENU, 100), SH_MENU_ACT_NONE);
    CHECK_EQ(u.m.mode, SH_MENU_EDIT);
}

/* The labels of the normal screen after the codes: HoL, AP, FUp — 1 s each, in this order. */
static void test_display_labels(void)
{
    CHECK_EQ(sh_menu_label_at(false, false, false, 0), SH_MENU_LABEL_NONE);
    CHECK_EQ(sh_menu_label_at(false, true, false, 0), SH_MENU_LABEL_AP);
    CHECK_EQ(sh_menu_label_at(false, true, false, 1), SH_MENU_LABEL_NONE);
    CHECK_EQ(sh_menu_label_at(true, false, false, 0), SH_MENU_LABEL_HOL);
    CHECK_EQ(sh_menu_label_at(true, false, false, 1), SH_MENU_LABEL_NONE);
    CHECK_EQ(sh_menu_label_at(true, true, false, 0), SH_MENU_LABEL_HOL);
    CHECK_EQ(sh_menu_label_at(true, true, false, 1), SH_MENU_LABEL_AP);
    CHECK_EQ(sh_menu_label_at(true, true, false, 2), SH_MENU_LABEL_NONE);
    CHECK_EQ(sh_menu_label_at(false, false, true, 0), SH_MENU_LABEL_FUP);
    CHECK_EQ(sh_menu_label_at(true, true, true, 0), SH_MENU_LABEL_HOL);
    CHECK_EQ(sh_menu_label_at(true, true, true, 1), SH_MENU_LABEL_AP);
    CHECK_EQ(sh_menu_label_at(true, true, true, 2), SH_MENU_LABEL_FUP);
    CHECK_EQ(sh_menu_label_at(true, true, true, 3), SH_MENU_LABEL_NONE);
    CHECK_EQ(sh_menu_label_at(false, true, true, 1), SH_MENU_LABEL_FUP);

    /* with the codes before them: E1 and Hot, then the labels (as sh_display counts) */
    const uint8_t alarms = BIT(SH_ALARM_E1) | BIT(SH_ALARM_HOT);
    CHECK_EQ(sh_alarm_code_at(alarms, 0), SH_ALARM_E1);
    CHECK_EQ(sh_alarm_code_at(alarms, 1000), SH_ALARM_HOT);
    CHECK_EQ(sh_alarm_code_at(alarms, 2000), SH_ALARM_COUNT);
    CHECK_EQ(sh_menu_label_at(false, true, false, 2000 / SH_ALARM_CODE_MS - sh_alarm_count(alarms)), SH_MENU_LABEL_AP);
    CHECK_EQ(sh_menu_label_at(false, true, false, 3000 / SH_ALARM_CODE_MS - sh_alarm_count(alarms)), SH_MENU_LABEL_NONE);
}

/* ---- the network mode ------------------------------------------------------------------- */

static void test_net_ap_times(void)
{
    sh_net_t n = { 0 };
    CHECK_EQ(n.mode, SH_NET_CP);                                /* the mode is not stored: CP at boot */
    CHECK_EQ(sh_net_step(&n, false, true, 100), SH_NET_ACT_NONE);

    CHECK_EQ(sh_net_choose(&n, SH_NET_AP, false, 1000), SH_NET_ACT_START_AP);
    CHECK_EQ(n.mode, SH_NET_AP);
    CHECK_EQ(sh_net_choose(&n, SH_NET_AP, false, 1001), SH_NET_ACT_NONE);       /* already AP */
    CHECK_EQ(sh_net_event(&n, SH_NET_EVT_AP_STARTED, true, 1002), SH_NET_ACT_NONE);

    /* 15 min without a client, counted from the start: +-1 s */
    CHECK_EQ(SH_NET_AP_IDLE_S, 900);
    CHECK_EQ(sh_net_step(&n, false, true, 1002 + 899), SH_NET_ACT_NONE);
    CHECK_EQ(n.mode, SH_NET_AP);
    CHECK_EQ(sh_net_step(&n, false, true, 1002 + 900), SH_NET_ACT_START_CP);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK(!n.radio_off);
    CHECK_EQ(sh_net_step(&n, false, true, 1002 + 901), SH_NET_ACT_NONE);        /* once */

    /* a phone on it: no limit; 15 min after the last one leaves */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, false, 0);
    sh_net_event(&n, SH_NET_EVT_AP_STARTED, true, 1);
    sh_net_event(&n, SH_NET_EVT_CLIENT_JOINED, true, 600);
    CHECK_EQ(sh_net_step(&n, false, true, 5000), SH_NET_ACT_NONE);
    CHECK_EQ(n.mode, SH_NET_AP);
    sh_net_event(&n, SH_NET_EVT_CLIENT_JOINED, true, 5100);                     /* a second one */
    sh_net_event(&n, SH_NET_EVT_CLIENT_LEFT, true, 5200);
    CHECK_EQ(sh_net_step(&n, false, true, 5200 + 900), SH_NET_ACT_NONE);        /* one is still there */
    sh_net_event(&n, SH_NET_EVT_CLIENT_LEFT, true, 7000);
    CHECK_EQ(sh_net_step(&n, false, true, 7000 + 899), SH_NET_ACT_NONE);
    CHECK_EQ(sh_net_step(&n, false, true, 7000 + 900), SH_NET_ACT_START_CP);

    /* a new phone within the 15 min starts the count over */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, false, 0);
    sh_net_event(&n, SH_NET_EVT_CLIENT_JOINED, true, 890);
    sh_net_event(&n, SH_NET_EVT_CLIENT_LEFT, true, 895);
    CHECK_EQ(sh_net_step(&n, false, true, 900), SH_NET_ACT_NONE);
    CHECK_EQ(sh_net_step(&n, false, true, 895 + 899), SH_NET_ACT_NONE);
    CHECK_EQ(sh_net_step(&n, false, true, 895 + 900), SH_NET_ACT_START_CP);

    /* a "left" without a "joined" does not underflow */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, false, 0);
    sh_net_event(&n, SH_NET_EVT_CLIENT_LEFT, true, 500);
    CHECK_EQ(n.clients, 0);
    CHECK_EQ(sh_net_step(&n, false, true, 900), SH_NET_ACT_START_CP);
}

static void test_net_back_to_cp(void)
{
    /* the time is over, a network is stored: the router */
    sh_net_t n = { 0 };
    sh_net_choose(&n, SH_NET_AP, false, 0);
    CHECK_EQ(sh_net_step(&n, false, true, 900), SH_NET_ACT_START_CP);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK(!n.radio_off);

    /* no stored network: the radio goes off (no setup portal for another 15 min) */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, false, 0);
    CHECK_EQ(sh_net_step(&n, false, false, 900), SH_NET_ACT_RADIO_OFF);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK(n.radio_off);
    CHECK_EQ(sh_net_step(&n, false, false, 5000), SH_NET_ACT_NONE);             /* it stays off */
    /* ... until a choice of the menu: CP -> the portal, AP -> the access point */
    CHECK_EQ(sh_net_choose(&n, SH_NET_CP, false, 6000), SH_NET_ACT_START_CP);
    CHECK(!n.radio_off);
    n.radio_off = true;
    CHECK_EQ(sh_net_choose(&n, SH_NET_AP, false, 6000), SH_NET_ACT_START_AP);
    CHECK(!n.radio_off);

    /* captive-wifi's own idle time came first (the radio is off already) */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, false, 0);
    CHECK_EQ(sh_net_event(&n, SH_NET_EVT_AP_TIMEOUT, true, 899), SH_NET_ACT_START_CP);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK_EQ(sh_net_step(&n, false, true, 900), SH_NET_ACT_NONE);               /* not twice */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, false, 0);
    CHECK_EQ(sh_net_event(&n, SH_NET_EVT_AP_TIMEOUT, false, 899), SH_NET_ACT_NONE);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK(n.radio_off);
    /* ... and after ours: nothing more */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, false, 0);
    sh_net_step(&n, false, true, 900);
    CHECK_EQ(sh_net_event(&n, SH_NET_EVT_AP_TIMEOUT, true, 901), SH_NET_ACT_NONE);

    /* CP chosen in AP: back at once */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, false, 0);
    sh_net_event(&n, SH_NET_EVT_CLIENT_JOINED, true, 10);
    CHECK_EQ(sh_net_choose(&n, SH_NET_CP, false, 20), SH_NET_ACT_START_CP);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK_EQ(sh_net_step(&n, false, true, 5000), SH_NET_ACT_NONE);

    /* CP chosen in CP with the radio on: nothing is restarted */
    memset(&n, 0, sizeof(n));
    CHECK_EQ(sh_net_choose(&n, SH_NET_CP, false, 0), SH_NET_ACT_NONE);

    /* CP without a stored network: the portal ran out of time -> off until a choice of nEt */
    CHECK_EQ(sh_net_event(&n, SH_NET_EVT_PORTAL_TIMEOUT, false, 900), SH_NET_ACT_NONE);
    CHECK(n.radio_off);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK_EQ(sh_net_step(&n, false, false, 90000), SH_NET_ACT_NONE);
    CHECK_EQ(sh_net_choose(&n, SH_NET_CP, false, 90001), SH_NET_ACT_START_CP);
    CHECK(!n.radio_off);
}

/* AP chosen during a firmware update waits for its end. */
static void test_net_ap_postponed(void)
{
    sh_net_t n = { 0 };
    CHECK_EQ(sh_net_choose(&n, SH_NET_AP, true, 100), SH_NET_ACT_NONE);
    CHECK_EQ(n.mode, SH_NET_CP);
    CHECK(n.ap_pending);
    CHECK_EQ(sh_net_step(&n, true, true, 150), SH_NET_ACT_NONE);                /* still updating */
    CHECK_EQ(sh_net_step(&n, false, true, 200), SH_NET_ACT_START_AP);           /* it is over (failed) */
    CHECK_EQ(n.mode, SH_NET_AP);
    CHECK(!n.ap_pending);
    CHECK_EQ(sh_net_step(&n, false, true, 200 + 899), SH_NET_ACT_NONE);         /* the 15 min from then */
    CHECK_EQ(sh_net_step(&n, false, true, 200 + 900), SH_NET_ACT_START_CP);

    /* CP chosen meanwhile: the postponed AP is dropped */
    memset(&n, 0, sizeof(n));
    sh_net_choose(&n, SH_NET_AP, true, 100);
    CHECK_EQ(sh_net_choose(&n, SH_NET_CP, true, 110), SH_NET_ACT_NONE);
    CHECK(!n.ap_pending);
    CHECK_EQ(sh_net_step(&n, false, true, 200), SH_NET_ACT_NONE);

    /* the update decides: sh_ota_updating() */
    sh_ota_t o = { .phase = SH_OTA_RUNNING };
    CHECK(sh_ota_updating(&o));
    o.phase = SH_OTA_DONE;                                      /* until the restart */
    CHECK(sh_ota_updating(&o));
    o.phase = SH_OTA_WAITING;                                   /* waiting for the pump is not the update */
    CHECK(!sh_ota_updating(&o));
    o.phase = SH_OTA_CHECKING;
    CHECK(!sh_ota_updating(&o));
    o.phase = SH_OTA_FAILED;
    CHECK(!sh_ota_updating(&o));
}

static void test_net_name(void)
{
    char name[SH_NET_NAME_MAX];
    sh_net_hostname(name, sizeof(name), "SOLAR-810841");
    CHECK(strcmp(name, "solar-810841") == 0);
    sh_net_hostname(name, sizeof(name), "SOLAR-A1B2C3");
    CHECK(strcmp(name, "solar-a1b2c3") == 0);
    CHECK(strcmp(sh_net_code("SOLAR-810841"), "810841") == 0);
    CHECK(strcmp(sh_net_code("SOLAR-A1B2C3"), "A1B2C3") == 0);
    CHECK(strcmp(sh_net_code("841"), "841") == 0);
    CHECK_EQ(SH_NET_CODE_LEN, SH_WEB_CODE_LEN);

    char small[8];
    memset(small, 'x', sizeof(small));
    sh_net_hostname(small, 6, "SOLAR-810841");                  /* cut, with its NUL */
    CHECK(strcmp(small, "solar") == 0);
    CHECK_EQ(small[6], 'x');
}

/* ---- the indication --------------------------------------------------------------------- */

static const sh_ind_params_t P = { .cold = 200, .hot = 900 };

static sh_ind_out_t ind(const sh_ind_in_t *in)
{
    sh_ind_state_t st = { 0 };
    return sh_ind_update(&st, &P, in);
}

static void test_ind_ap_ota(void)
{
    sh_ind_in_t in;
    memset(&in, 0, sizeof(in));
    in.t_col = 500;
    in.ap = true;                                               /* in AP there is no router */

    /* breathing in the colour of the process */
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATH);
    CHECK_EQ(ind(&in).color, SH_COLOR_BLUE);
    in.pump_on = true;
    CHECK_EQ(ind(&in).color, SH_COLOR_GREEN);
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATH);
    in.alarms = BIT(SH_ALARM_HOT);
    CHECK_EQ(ind(&in).color, SH_COLOR_ORANGE);
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATH);
    in.alarms = 0;
    in.pump_on = false;
    in.vacation = true;
    CHECK_EQ(ind(&in).color, SH_COLOR_PURPLE);
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATH);
    in.vacation = false;
    in.t_col = 950;
    CHECK_EQ(ind(&in).color, SH_COLOR_YELLOW);
    in.t_col = 100;
    CHECK_EQ(ind(&in).color, SH_COLOR_WHITE);
    CHECK_EQ(ind(&in).pattern, SH_IND_BREATH);

    /* the alarm patterns come first */
    in.alarms = BIT(SH_ALARM_E4);
    CHECK_EQ(ind(&in).color, SH_COLOR_RED);
    CHECK_EQ(ind(&in).pattern, SH_IND_FAST);
    in.alarms = BIT(SH_ALARM_E1);
    CHECK_EQ(ind(&in).pattern, SH_IND_DOUBLE);
    in.alarms = BIT(SH_ALARM_E8);
    CHECK_EQ(ind(&in).pattern, SH_IND_DOUBLE);

    /* the update comes before everything: white, 500 / 500 ms */
    in.ota = true;
    CHECK_EQ(ind(&in).color, SH_COLOR_WHITE);
    CHECK_EQ(ind(&in).pattern, SH_IND_BLINK);
    memset(&in, 0, sizeof(in));
    in.t_col = 500;
    in.wifi_connected = true;
    in.pump_on = true;
    in.ota = true;
    CHECK_EQ(ind(&in).color, SH_COLOR_WHITE);
    CHECK_EQ(ind(&in).pattern, SH_IND_BLINK);
    in.alarms = BIT(SH_ALARM_E5) | BIT(SH_ALARM_E2);
    CHECK_EQ(ind(&in).color, SH_COLOR_WHITE);
    CHECK_EQ(ind(&in).pattern, SH_IND_BLINK);

    /* the end of AP: the indication is gone (steady on the router, heartbeat without it) */
    memset(&in, 0, sizeof(in));
    in.t_col = 500;
    in.wifi_connected = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_STEADY);
    in.wifi_connected = false;
    CHECK_EQ(ind(&in).pattern, SH_IND_HEARTBEAT);

    /* blink: 500 on / 500 off */
    CHECK(sh_ind_lit(SH_IND_BLINK, 0));
    CHECK(sh_ind_lit(SH_IND_BLINK, 499));
    CHECK(!sh_ind_lit(SH_IND_BLINK, 500));
    CHECK(!sh_ind_lit(SH_IND_BLINK, 999));
    CHECK(sh_ind_lit(SH_IND_BLINK, 1000));
    CHECK_EQ(sh_ind_level(SH_IND_BLINK, 100), 100);
    CHECK_EQ(sh_ind_level(SH_IND_BLINK, 600), 0);

    /* breathing: from dark to the full brightness and back in 5 s, never jumping */
    CHECK_EQ(sh_ind_level(SH_IND_BREATH, 0), 0);
    CHECK_EQ(sh_ind_level(SH_IND_BREATH, SH_IND_BREATH_MS / 2), 100);
    CHECK_EQ(sh_ind_level(SH_IND_BREATH, SH_IND_BREATH_MS), 0);
    CHECK_EQ(sh_ind_level(SH_IND_BREATH, SH_IND_BREATH_MS / 4), 25);    /* squared ramp */
    int prev = 0;
    for (uint32_t ms = 0; ms <= SH_IND_BREATH_MS; ms += 10) {
        const int l = sh_ind_level(SH_IND_BREATH, ms);
        CHECK(l >= 0 && l <= 100);
        CHECK(ms <= SH_IND_BREATH_MS / 2 ? l >= prev : l <= prev);
        CHECK(l - prev <= 2 && prev - l <= 2);
        CHECK_EQ(l, sh_ind_level(SH_IND_BREATH, SH_IND_BREATH_MS - ms));        /* symmetric */
        CHECK_EQ(l, sh_ind_level(SH_IND_BREATH, ms + 3 * SH_IND_BREATH_MS));    /* periodic */
        prev = l;
    }
    /* the other patterns: fully on or off */
    CHECK_EQ(sh_ind_level(SH_IND_STEADY, 12345), 100);
    CHECK_EQ(sh_ind_level(SH_IND_HEARTBEAT, 79), 100);
    CHECK_EQ(sh_ind_level(SH_IND_HEARTBEAT, 80), 0);
    CHECK_EQ(sh_ind_level(SH_IND_FAST, 150), 0);
    CHECK_EQ(sh_ind_level(SH_IND_DOUBLE, 250), 100);
}

/* ---- the firmware update ---------------------------------------------------------------- */

static const sh_ota_in_t ONLINE = { .online = true };

/* the automatic check found 0.2.0 at `*now` (just after the first minute) */
static sh_ota_t offered(uint32_t *now)
{
    sh_ota_t o = { 0 };
    *now = SH_OTA_BOOT_CHECK_MS;
    sh_ota_step(&o, &ONLINE, *now);
    *now += 2000;
    sh_ota_event(&o, SH_OTA_EVT_AVAILABLE, "0.2.0", 0, *now);
    return o;
}

static void test_ota_boot_check(void)
{
    /* 1 min after boot, +-1 s, once, only on the router */
    sh_ota_t o = { 0 };
    CHECK_EQ(SH_OTA_BOOT_CHECK_MS, 60000);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 0), SH_OTA_ACT_NONE);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 59000), SH_OTA_ACT_NONE);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 59999), SH_OTA_ACT_NONE);
    CHECK_EQ(o.phase, SH_OTA_IDLE);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 60000), SH_OTA_ACT_CHECK);
    CHECK_EQ(o.phase, SH_OTA_CHECKING);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 60200), SH_OTA_ACT_NONE);
    sh_ota_event(&o, SH_OTA_EVT_CURRENT, "0.1.0", 0, 61000);
    CHECK_EQ(o.phase, SH_OTA_CURRENT);
    CHECK(!o.offer);
    for (uint32_t t = 61000; t < 400000; t += 1000) {           /* never again by itself */
        CHECK_EQ(sh_ota_step(&o, &ONLINE, t), SH_OTA_ACT_NONE);
    }

    /* not on the router at that moment (AP, the portal, no Wi-Fi): no check, not later either */
    memset(&o, 0, sizeof(o));
    const sh_ota_in_t offline = { .online = false };
    CHECK_EQ(sh_ota_step(&o, &offline, 60000), SH_OTA_ACT_NONE);
    CHECK_EQ(o.phase, SH_OTA_IDLE);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 61000), SH_OTA_ACT_NONE);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 600000), SH_OTA_ACT_NONE);

    /* the first step comes late (a slow start): still once */
    memset(&o, 0, sizeof(o));
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 60150), SH_OTA_ACT_CHECK);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 60350), SH_OTA_ACT_NONE);

    /* a failed automatic check: only the log — nothing on the display or the page */
    memset(&o, 0, sizeof(o));
    sh_ota_step(&o, &ONLINE, 60000);
    sh_ota_event(&o, SH_OTA_EVT_FAILED, NULL, 0, 65000);
    CHECK_EQ(o.phase, SH_OTA_IDLE);
    CHECK(!o.offer);
    CHECK(!sh_ota_label(&o));
    CHECK(strcmp(sh_web_ota_key(o.phase, false), "idle") == 0);
}

static void test_ota_fup(void)
{
    /* a newer version: "FUp" for 5 s */
    uint32_t now;
    sh_ota_t o = offered(&now);
    CHECK_EQ(o.phase, SH_OTA_AVAILABLE);
    CHECK(o.offer);
    CHECK(strcmp(o.version, "0.2.0") == 0);
    CHECK_EQ(SH_OTA_OFFER_MS, 5000);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, now + 4999), SH_OTA_ACT_NONE);
    CHECK(o.offer);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, now + 5000), SH_OTA_ACT_NONE);
    CHECK(!o.offer);

    /* without the confirmation nothing is installed; the version stays for the web interface */
    CHECK_EQ(o.phase, SH_OTA_AVAILABLE);
    for (uint32_t t = now + 5000; t < now + 100000; t += 1000) {
        CHECK_EQ(sh_ota_step(&o, &ONLINE, t), SH_OTA_ACT_NONE);
    }
    CHECK_EQ(o.phase, SH_OTA_AVAILABLE);
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CONFIRM, &ONLINE, now + 100000), SH_OTA_ACT_NONE);   /* too late */
    CHECK_EQ(o.phase, SH_OTA_AVAILABLE);

    /* short "menu" within the 5 s: the update starts at once (pump stopped, no protection) */
    o = offered(&now);
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CONFIRM, &ONLINE, now + 3000), SH_OTA_ACT_UPDATE);
    CHECK_EQ(o.phase, SH_OTA_RUNNING);
    CHECK(sh_ota_updating(&o));
    CHECK(sh_ota_label(&o));
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CONFIRM, &ONLINE, now + 3200), SH_OTA_ACT_NONE);     /* once */
    CHECK_EQ(sh_ota_step(&o, &ONLINE, now + 3400), SH_OTA_ACT_NONE);
    /* general-ota checks again before the download, then reports the progress */
    sh_ota_event(&o, SH_OTA_EVT_AVAILABLE, "0.2.0", 0, now + 4000);
    CHECK_EQ(o.phase, SH_OTA_RUNNING);
    CHECK(o.offer);                                             /* "FUp" stays until its 5 s are over */
    sh_ota_event(&o, SH_OTA_EVT_PROGRESS, "0.2.0", 35, now + 20000);
    CHECK_EQ(o.pct, 35);
    sh_ota_event(&o, SH_OTA_EVT_PROGRESS, "0.2.0", -1, now + 21000);    /* size unknown */
    CHECK_EQ(o.pct, -1);
    sh_ota_event(&o, SH_OTA_EVT_SUCCESS, "0.2.0", 100, now + 60000);
    CHECK_EQ(o.phase, SH_OTA_DONE);
    CHECK(sh_ota_updating(&o));                                 /* until the restart */

    /* a failed update: the running version stays, a new check is possible */
    o = offered(&now);
    sh_ota_cmd(&o, SH_OTA_CMD_CONFIRM, &ONLINE, now + 1000);
    sh_ota_event(&o, SH_OTA_EVT_PROGRESS, NULL, 40, now + 10000);
    sh_ota_event(&o, SH_OTA_EVT_FAILED, NULL, 0, now + 12000);
    CHECK_EQ(o.phase, SH_OTA_FAILED);
    CHECK(!sh_ota_updating(&o));
    CHECK(!sh_ota_label(&o));
    CHECK_EQ(sh_ota_step(&o, &ONLINE, now + 13000), SH_OTA_ACT_NONE);   /* no retry by itself */
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &ONLINE, now + 20000), SH_OTA_ACT_CHECK);

    /* the server image was replaced by the running version meanwhile: nothing to install */
    o = offered(&now);
    sh_ota_cmd(&o, SH_OTA_CMD_CONFIRM, &ONLINE, now + 1000);
    sh_ota_event(&o, SH_OTA_EVT_CURRENT, "0.1.0", 0, now + 3000);
    CHECK_EQ(o.phase, SH_OTA_CURRENT);
    CHECK_EQ(o.version[0], '\0');
}

static void test_ota_web(void)
{
    /* by hand: check -> up to date */
    sh_ota_t o = { .boot_done = true };
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_UPDATE, &ONLINE, 100000), SH_OTA_ACT_NONE);  /* nothing found yet */
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &ONLINE, 100000), SH_OTA_ACT_CHECK);
    CHECK_EQ(o.phase, SH_OTA_CHECKING);
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &ONLINE, 100100), SH_OTA_ACT_NONE);   /* one at a time */
    sh_ota_event(&o, SH_OTA_EVT_CURRENT, "0.1.0", 0, 102000);
    CHECK_EQ(o.phase, SH_OTA_CURRENT);

    /* check -> a newer version: no "FUp" on the display, the "Update" button installs it */
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &ONLINE, 200000), SH_OTA_ACT_CHECK);
    sh_ota_event(&o, SH_OTA_EVT_AVAILABLE, "0.2.0", 0, 202000);
    CHECK_EQ(o.phase, SH_OTA_AVAILABLE);
    CHECK(!o.offer);
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CONFIRM, &ONLINE, 203000), SH_OTA_ACT_NONE); /* nothing is offered */
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_UPDATE, &ONLINE, 204000), SH_OTA_ACT_UPDATE);
    CHECK_EQ(o.phase, SH_OTA_RUNNING);
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &ONLINE, 205000), SH_OTA_ACT_NONE);   /* not during it */
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_UPDATE, &ONLINE, 205000), SH_OTA_ACT_NONE);

    /* CP without internet: the check fails -> "no connection to the update server" */
    memset(&o, 0, sizeof(o));
    o.boot_done = true;
    sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &ONLINE, 100000);
    sh_ota_event(&o, SH_OTA_EVT_FAILED, NULL, 0, 105000);
    CHECK_EQ(o.phase, SH_OTA_NO_SERVER);
    CHECK(strcmp(sh_web_ota_key(o.phase, false), "noconn") == 0);
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &ONLINE, 110000), SH_OTA_ACT_CHECK);  /* again */

    /* not on the router (AP): nothing is checked */
    memset(&o, 0, sizeof(o));
    o.boot_done = true;
    const sh_ota_in_t offline = { .online = false };
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &offline, 100000), SH_OTA_ACT_NONE);
    CHECK_EQ(o.phase, SH_OTA_IDLE);

    /* the keys of the page: in AP "no internet (own access point)" instead of the results */
    CHECK(strcmp(sh_web_ota_key(SH_OTA_IDLE, true), "ap") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_CURRENT, true), "ap") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_AVAILABLE, true), "ap") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_NO_SERVER, true), "ap") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_FAILED, true), "ap") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_WAITING, true), "wait") == 0);   /* a confirmed one keeps waiting */
    CHECK(strcmp(sh_web_ota_key(SH_OTA_IDLE, false), "idle") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_CHECKING, false), "check") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_CURRENT, false), "ok") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_AVAILABLE, false), "new") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_WAITING, false), "wait") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_RUNNING, false), "run") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_DONE, false), "done") == 0);
    CHECK(strcmp(sh_web_ota_key(SH_OTA_FAILED, false), "fail") == 0);
    CHECK(strcmp(sh_web_ota_key((sh_ota_phase_t)99, false), "idle") == 0);
}

/* The update starts only with the pump stopped and no protection active; otherwise it waits. */
static void test_ota_wait(void)
{
    sh_ota_in_t in = ONLINE;
    CHECK(sh_ota_may_start(&in));
    in.pump_on = true;
    CHECK(!sh_ota_may_start(&in));
    in = ONLINE;
    const sh_alarm_t protections[] = { SH_ALARM_HOT, SH_ALARM_FRO, SH_ALARM_E4, SH_ALARM_E5 };
    for (unsigned i = 0; i < sizeof(protections) / sizeof(protections[0]); i++) {
        in.alarms = (uint8_t)BIT(protections[i]);
        CHECK(!sh_ota_may_start(&in));
    }
    in.alarms = BIT(SH_ALARM_E1) | BIT(SH_ALARM_E2) | BIT(SH_ALARM_E6) | BIT(SH_ALARM_E8);
    CHECK(sh_ota_may_start(&in));                               /* a fault is not a protection */
    in = ONLINE;
    in.air = true;                                              /* the air venting, also in its pauses */
    CHECK(!sh_ota_may_start(&in));
    in = ONLINE;
    in.online = false;
    CHECK(!sh_ota_may_start(&in));

    /* confirmed while the pump runs: it waits ("FUp" among the labels), then starts by itself */
    uint32_t now;
    sh_ota_t o = offered(&now);
    in = ONLINE;
    in.pump_on = true;
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_CONFIRM, &in, now + 1000), SH_OTA_ACT_NONE);
    CHECK_EQ(o.phase, SH_OTA_WAITING);
    CHECK(sh_ota_label(&o));
    CHECK(!sh_ota_updating(&o));                                /* no white LED yet, AP not postponed */
    for (uint32_t t = now + 1200; t < now + 3600000; t += 60000) {
        CHECK_EQ(sh_ota_step(&o, &in, t), SH_OTA_ACT_NONE);
    }
    CHECK_EQ(o.phase, SH_OTA_WAITING);
    in.pump_on = false;
    in.alarms = BIT(SH_ALARM_E4);                               /* the pump stopped by the ceiling */
    CHECK_EQ(sh_ota_step(&o, &in, now + 3600000), SH_OTA_ACT_NONE);
    in.alarms = 0;
    CHECK_EQ(sh_ota_step(&o, &in, now + 3600200), SH_OTA_ACT_UPDATE);
    CHECK_EQ(o.phase, SH_OTA_RUNNING);
    CHECK_EQ(sh_ota_step(&o, &in, now + 3600400), SH_OTA_ACT_NONE);     /* once */
    in.pump_on = true;                                          /* the pump logic goes on during it */
    CHECK_EQ(sh_ota_step(&o, &in, now + 3600600), SH_OTA_ACT_NONE);
    CHECK_EQ(o.phase, SH_OTA_RUNNING);

    /* the same from the web interface: "Update" with the frost protection running */
    memset(&o, 0, sizeof(o));
    o.boot_done = true;
    sh_ota_cmd(&o, SH_OTA_CMD_CHECK, &ONLINE, 100000);
    sh_ota_event(&o, SH_OTA_EVT_AVAILABLE, "0.3.0", 0, 101000);
    in = ONLINE;
    in.pump_on = true;
    in.alarms = BIT(SH_ALARM_FRO);
    CHECK_EQ(sh_ota_cmd(&o, SH_OTA_CMD_UPDATE, &in, 102000), SH_OTA_ACT_NONE);
    CHECK_EQ(o.phase, SH_OTA_WAITING);
    CHECK(strcmp(sh_web_ota_key(o.phase, false), "wait") == 0);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 103000), SH_OTA_ACT_UPDATE);

    /* waiting and the router is lost (or AP was chosen): it starts when the router is back */
    memset(&o, 0, sizeof(o));
    o.boot_done = true;
    o.phase = SH_OTA_WAITING;
    in = ONLINE;
    in.online = false;
    CHECK_EQ(sh_ota_step(&o, &in, 200000), SH_OTA_ACT_NONE);
    CHECK_EQ(sh_ota_step(&o, &ONLINE, 201000), SH_OTA_ACT_UPDATE);
}

/* ---- the web interface ------------------------------------------------------------------ */

static char s_json[SH_WEB_STATUS_MAX];

static const char *json(const sh_web_status_t *s)
{
    CHECK(sh_web_status_json(s_json, sizeof(s_json), s) > 0);
    return s_json;
}

#define HAS(s, text)    CHECK(strstr(json(s), text) != NULL)

static void test_web_status(void)
{
    sh_web_status_t s = {
        .t_col = 523, .t_tank = 418,
        .cfg = { .settings = S, .frost = true,
                 .inst = { .v = { SH_FLUID_WATER, 0, 15, 120, SH_COL_FLAT, 20, 150, 20 } } },
        .col_protect = 1100, .name = "solar-810841", .version = "0.1.0", .serial = "E8F60A810841",
        .ssid = "Home", .ip = "192.168.1.7", .rssi = -41, .host = "solar-810841", .ap_ssid = "SOLAR-810841",
        .clock = { .trust = SH_CLOCK_UNSET, .min = -1, .tz = "UTC", .peak = SH_PEAK_NONE },
        .cool_until = -1, .sunrise = -1, .sunset = -1, .e8_normal = INT16_MIN,
    };
    /* the functions of this step are there: nothing of theirs is grey */
    HAS(&s, "\"net_mode\"");
    HAS(&s, "\"ota\"]");

    /* CP on the router: the mode, the name, nothing checked yet */
    HAS(&s, "\"name\":\"solar-810841\",");
    HAS(&s, "\"net\":{\"ssid\":\"Home\",\"ip\":\"192.168.1.7\",\"rssi\":-41,\"mode\":\"cp\",\"pend\":0,"
            "\"host\":\"solar-810841\",\"ap\":\"SOLAR-810841\"},"
            "\"ota\":{\"st\":\"idle\",\"ver\":\"\",\"pct\":null},");

    /* a newer version, then the update with its progress */
    s.ota = SH_OTA_AVAILABLE;
    s.ota_version = "0.2.0";
    HAS(&s, "\"ota\":{\"st\":\"new\",\"ver\":\"0.2.0\",\"pct\":null},");
    s.ota = SH_OTA_WAITING;
    HAS(&s, "\"ota\":{\"st\":\"wait\",\"ver\":\"0.2.0\",\"pct\":null},");
    s.ota = SH_OTA_RUNNING;
    s.ota_pct = 35;
    HAS(&s, "\"ota\":{\"st\":\"run\",\"ver\":\"0.2.0\",\"pct\":35},");
    s.ota_pct = -1;                                             /* size unknown */
    HAS(&s, "\"ota\":{\"st\":\"run\",\"ver\":\"0.2.0\",\"pct\":null},");
    s.ap_pending = true;                                        /* AP chosen meanwhile: after it */
    HAS(&s, "\"mode\":\"cp\",\"pend\":1,");
    s.ap_pending = false;
    s.ota = SH_OTA_CURRENT;
    s.ota_version = "";
    HAS(&s, "\"ota\":{\"st\":\"ok\",\"ver\":\"\",\"pct\":null},");
    s.ota = SH_OTA_NO_SERVER;                                   /* CP without internet */
    HAS(&s, "\"ota\":{\"st\":\"noconn\",");

    /* AP: its address, no router, no internet for the update */
    s.ap = true;
    s.ssid = "";
    s.ip = "192.168.4.1";
    s.rssi = 0;
    s.ota = SH_OTA_CURRENT;
    HAS(&s, "\"net\":{\"ssid\":\"\",\"ip\":\"192.168.4.1\",\"rssi\":0,\"mode\":\"ap\",\"pend\":0,"
            "\"host\":\"solar-810841\",\"ap\":\"SOLAR-810841\"},"
            "\"ota\":{\"st\":\"ap\",\"ver\":\"\",\"pct\":null},");
}

static void test_web_requests(void)
{
    /* the network mode: with the access code only */
    sh_net_mode_t mode = SH_NET_CP;
    CHECK_EQ(sh_web_net("mode=ap&code=" CODE, CODE, &mode), SH_WEB_OK);
    CHECK_EQ(mode, SH_NET_AP);
    CHECK_EQ(sh_web_net("code=" CODE "&mode=cp", CODE, &mode), SH_WEB_OK);
    CHECK_EQ(mode, SH_NET_CP);
    CHECK_EQ(sh_web_net("mode=ap", CODE, &mode), SH_WEB_DENIED);
    CHECK_EQ(sh_web_net("mode=ap&code=123456", CODE, &mode), SH_WEB_DENIED);
    CHECK_EQ(sh_web_net("mode=off&code=" CODE, CODE, &mode), SH_WEB_BAD);
    CHECK_EQ(sh_web_net("mode=AP&code=" CODE, CODE, &mode), SH_WEB_BAD);
    CHECK_EQ(sh_web_net("code=" CODE, CODE, &mode), SH_WEB_BAD);
    CHECK_EQ(sh_web_net("", CODE, &mode), SH_WEB_BAD);

    /* the update: check, update — with the access code only */
    sh_ota_cmd_t cmd = SH_OTA_CMD_CONFIRM;
    CHECK_EQ(sh_web_ota("cmd=check&code=" CODE, CODE, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_OTA_CMD_CHECK);
    CHECK_EQ(sh_web_ota("cmd=update&code=" CODE, CODE, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_OTA_CMD_UPDATE);
    CHECK_EQ(sh_web_ota("cmd=update", CODE, &cmd), SH_WEB_DENIED);
    CHECK_EQ(sh_web_ota("cmd=check&code=000000", CODE, &cmd), SH_WEB_DENIED);
    CHECK_EQ(sh_web_ota("cmd=confirm&code=" CODE, CODE, &cmd), SH_WEB_BAD);     /* the button only */
    CHECK_EQ(sh_web_ota("code=" CODE, CODE, &cmd), SH_WEB_BAD);
}

int main(void)
{
    test_menu_net();
    test_menu_fup();
    test_display_labels();
    test_net_ap_times();
    test_net_back_to_cp();
    test_net_ap_postponed();
    test_net_name();
    test_ind_ap_ota();
    test_ota_boot_check();
    test_ota_fup();
    test_ota_web();
    test_ota_wait();
    test_web_status();
    test_web_requests();
    return test_summary("test_net");
}
