/* Host test of the web interface data and request checks (main/sh_web_api.c;
 * doc/tasks/step6-installer-web.md 3.4, step9-clock-history.md 3.4): the status JSON, the settings
 * ("set") with the checks of the menu, the access code, the pump test commands, the resets; the
 * clock, the run time today, the chart, the last days, the time zones, the time of the browser. */

#include <stdio.h>
#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_web_api.h"
#include "sh_tz.h"
#include "sh_cool.h"
#include "sh_heater.h"
#include "sh_pump_ctl.h"

#define CODE    "81084A"

/* The defaults of Kconfig */
static sh_web_config_t config(void)
{
    const sh_web_config_t c = {
        .settings = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } },
        .frost = true,
        .inst = { .v = { SH_FLUID_WATER, 0, 15, 120, SH_COL_FLAT, 20, 150, 20 } },
    };
    return c;
}

static sh_web_status_t status(void)
{
    const sh_web_status_t s = {
        .t_col = 523, .t_tank = 418, .pump_on = true, .reason = SH_PUMP_REASON_DIFF,
        .cfg = config(), .col_protect = 1100,
        .name = "Solar-810840", .version = "0.1.0", .serial = "E8F60A810840", .ssid = "Home", .ip = "192.168.1.7", .rssi = -41,
        .host = "Solar-810840", .ap_ssid = "Solar-810840",
        .clock = { .trust = SH_CLOCK_UNSET, .min = -1, .tz = "UTC", .peak = SH_PEAK_NONE },
        .cool_until = -1, .cool_learned = 663, .sunrise = -1, .sunset = -1, .e8_normal = INT16_MIN,
    };
    return s;
}

static char s_json[SH_WEB_STATUS_MAX];

static const char *json(const sh_web_status_t *s)
{
    const int len = sh_web_status_json(s_json, sizeof(s_json), s);
    CHECK(len > 0);
    CHECK_EQ(len, len > 0 ? (int)strlen(s_json) : -2);
    return s_json;
}

#define HAS(s, text)    CHECK(strstr(json(s), text) != NULL)
#define HAS_NOT(s, text) CHECK(strstr(json(s), text) == NULL)

static void test_status(void)
{
    sh_web_status_t s = status();
    CHECK(strcmp(json(&s),
        "{\"name\":\"Solar-810840\",\"fw\":\"0.1.0\",\"sn\":\"E8F60A810840\","
        "\"feat\":[\"alarms\",\"temps\",\"pump\",\"pump_test\",\"settings\",\"frost\",\"fluid\","
        "\"pressure\",\"col_max\",\"calc\",\"net\",\"fw\",\"reset\",\"clock\",\"timezone\",\"run_today\","
        "\"chart\",\"learned\",\"vacation\",\"season\",\"night_cool\",\"deaerate\",\"col_type\",\"tank_vol\","
        "\"vac_limit\",\"heater\",\"mount\",\"net_mode\",\"ota\"],"
        "\"tank\":418,\"col\":523,\"pump\":1,\"why\":\"diff\",\"alarms\":[],\"test\":0,\"left\":0,"
        "\"winter\":0,\"cool\":{\"on\":0,\"target\":null,\"until\":null,\"heater\":0,\"learned\":663,"
        "\"rise\":null,\"set\":null},"
        "\"air\":{\"on\":0,\"run\":0,\"cycle\":0,\"left\":0,\"cycles\":3,\"work\":5,\"pause\":10},"
        "\"heat\":{\"on\":0,\"det\":0,\"leak\":0,\"left\":0,\"last\":null,\"e8n\":null},"
        "\"pulse\":{\"on\":0,\"age\":null,\"at\":0,\"now\":0,\"next\":null,\"hot\":0},"
        "\"set\":{\"tbo\":{\"v\":70,\"min\":30,\"max\":85,\"step\":1},"
        "\"c_lo\":{\"v\":25,\"min\":10,\"max\":80,\"step\":1},"
        "\"d_on\":{\"v\":6,\"min\":5,\"max\":20,\"step\":1},"
        "\"d_of\":{\"v\":4,\"min\":1,\"max\":5,\"step\":1},"
        "\"t_st\":{\"v\":300,\"min\":60,\"max\":900,\"step\":30},"
        "\"c_lo_w\":{\"v\":15,\"min\":10,\"max\":80,\"step\":1},"
        "\"d_on_w\":{\"v\":5,\"min\":4,\"max\":20,\"step\":1},"
        "\"d_of_w\":{\"v\":3,\"min\":1,\"max\":4,\"step\":1},"
        "\"ssn\":{\"v\":2,\"min\":0,\"max\":2,\"step\":1},\"hol\":{\"v\":0,\"min\":0,\"max\":1,\"step\":1},"
        "\"nc\":{\"v\":1,\"min\":0,\"max\":1,\"step\":1},\"nc_w\":{\"v\":0,\"min\":0,\"max\":1,\"step\":1}},"
        "\"fro\":1,"
        "\"inst\":{\"fluid\":{\"v\":0,\"min\":0,\"max\":1},\"glycol\":{\"v\":0,\"min\":0,\"max\":60},"
        "\"press\":{\"v\":15,\"min\":5,\"max\":60},\"col_max\":{\"v\":120,\"min\":100,\"max\":250},"
        "\"col_type\":{\"v\":0,\"min\":0,\"max\":1},\"col_area\":{\"v\":20,\"min\":5,\"max\":200},"
        "\"tank_vol\":{\"v\":150,\"min\":50,\"max\":1000,\"step\":10},"
        "\"heater\":{\"v\":20,\"min\":0,\"max\":120,\"step\":5},\"mount\":{\"v\":0,\"min\":0,\"max\":1}},"
        "\"calc\":{\"boil\":1277,\"cap\":1150,\"prot\":1100,\"prot_set\":1100,\"frost\":40,\"vac\":900},"
        "\"net\":{\"ssid\":\"Home\",\"ip\":\"192.168.1.7\",\"rssi\":-41,\"mode\":\"cp\",\"pend\":0,"
        "\"host\":\"Solar-810840\",\"ap\":\"Solar-810840\"},"
        "\"ota\":{\"st\":\"idle\",\"ver\":\"\",\"pct\":null},"
        "\"clock\":{\"st\":\"unset\",\"sntp\":0,\"min\":null,\"tz\":\"UTC\",\"tzok\":0,\"peak\":null},"
        "\"run\":[0,0,0,0]}") == 0);
}

static void test_status_values(void)
{
    /* a faulty sensor or no value yet -> null ("---" on the page) */
    sh_web_status_t s = status();
    s.col_fault = true;
    HAS(&s, "\"tank\":418,\"col\":null,");
    s = status();
    s.t_tank = SH_TEMP_INVALID;
    HAS(&s, "\"tank\":null,\"col\":523,");
    s = status();
    s.t_col = -52;                              /* below zero */
    HAS(&s, "\"col\":-52,");

    /* the pump and its reason */
    s = status();
    s.pump_on = false;
    s.reason = SH_PUMP_REASON_TANK_MAX;
    HAS(&s, "\"pump\":0,\"why\":\"tmax\",");
    s.pump_on = true;
    s.reason = SH_PUMP_REASON_FROST;
    HAS(&s, "\"pump\":1,\"why\":\"frost\",");
    for (int r = 0; r < SH_PUMP_REASON_COUNT; r++) {    /* every reason has its own key */
        const char *key = sh_web_reason_key((sh_pump_reason_t)r);
        CHECK(r == SH_PUMP_REASON_NONE || strcmp(key, "none") != 0);
        for (int q = 0; q < r; q++) {
            CHECK(strcmp(key, sh_web_reason_key((sh_pump_reason_t)q)) != 0);
        }
    }
    CHECK(strcmp(sh_web_reason_key(SH_PUMP_REASON_COUNT), "none") == 0);

    /* the codes: the alarms by number, then the information */
    s = status();
    s.alarms = SH_ALARM_BIT(SH_ALARM_FRO) | SH_ALARM_BIT(SH_ALARM_E6) | SH_ALARM_BIT(SH_ALARM_E1) |
               SH_ALARM_BIT(SH_ALARM_HOT);
    HAS(&s, "\"alarms\":[\"E1\",\"E6\",\"Hot\",\"Fro\"],");
    s.alarms = SH_ALARM_BIT(SH_ALARM_E4);
    HAS(&s, "\"alarms\":[\"E4\"],");

    /* the pump test */
    s = status();
    s.test_on = true;
    s.test_left_s = 245;
    HAS(&s, "\"test\":1,\"left\":245,");

    /* the menu values with the range the others leave them; the frost switch */
    s = status();
    s.cfg.settings.v[SH_SET_DIFF_ON] = 12;
    s.cfg.settings.v[SH_SET_DIFF_OFF] = 9;
    s.cfg.frost = false;
    HAS(&s, "\"d_on\":{\"v\":12,\"min\":10,\"max\":20,\"step\":1}");
    HAS(&s, "\"d_of\":{\"v\":9,\"min\":1,\"max\":11,\"step\":1}");
    HAS(&s, "\"fro\":0,");

    /* the installer data and what is computed from them; the protection limited by the cap */
    s = status();
    s.cfg.inst = (sh_installer_t){ .v = { SH_FLUID_GLYCOL, 9, 22, 150, SH_COL_FLAT, 20, 150 } };
    s.col_protect = 1400;
    HAS(&s, "\"inst\":{\"fluid\":{\"v\":1,\"min\":0,\"max\":1},\"glycol\":{\"v\":9,");
    HAS(&s, "\"calc\":{\"boil\":1365,\"cap\":1265,\"prot\":1265,\"prot_set\":1400,\"frost\":13,\"vac\":1125}");

    /* text values are escaped; no network -> empty strings */
    s = status();
    s.ssid = "a\"b\\c\td";
    HAS(&s, "\"ssid\":\"a\\\"b\\\\c\\u0009d\"");
    s.ssid = "\xD0\x94\xD0\xBE\xD0\xBC";       /* UTF-8 passes */
    HAS(&s, "\"ssid\":\"\xD0\x94\xD0\xBE\xD0\xBC\"");
    s.ssid = "";
    s.ip = NULL;
    s.rssi = 0;
    HAS(&s, "\"net\":{\"ssid\":\"\",\"ip\":\"\",\"rssi\":0,");

    /* a missing function is not in the list (the page greys it out) */
    s = status();
    HAS(&s, "\"frost\"");
    HAS(&s, "\"vacation\"");                  /* group C: now there */
    HAS(&s, "\"season\"");
    HAS(&s, "\"night_cool\"");
    HAS(&s, "\"deaerate\"");
    HAS(&s, "\"col_type\"");
    HAS(&s, "\"tank_vol\"");
    HAS(&s, "\"vac_limit\"");
    HAS(&s, "\"net_mode\"");                    /* group E: now there */
    HAS(&s, "\"ota\"");
    HAS_NOT(&s, "\"city\"");
    HAS_NOT(&s, "\"homekit\"");

    /* "history" only with the history partition */
    HAS_NOT(&s, "\"history\"");
    s.history = true;
    HAS(&s, "\"heater\",\"mount\",\"net_mode\",\"ota\",\"history\"],");

    /* the clock and the run time today (whole minutes) */
    s = status();
    s.clock = (sh_web_clock_t){ .trust = SH_CLOCK_RELIABLE, .sntp = true, .min = 872, .tz = "Europe/Sofia",
                                .tz_confirmed = true, .peak = 790 };
    s.run_s[SH_RUN_CHARGE] = 7229;          /* 120.5 min */
    s.run_s[SH_RUN_PROTECT] = 29;
    s.run_s[SH_RUN_OTHER] = 30;
    s.run_s[SH_RUN_COOL] = 2400;            /* night cooling: its own number */
    HAS(&s, "\"clock\":{\"st\":\"ok\",\"sntp\":1,\"min\":872,\"tz\":\"Europe/Sofia\",\"tzok\":1,"
            "\"peak\":790},\"run\":[120,0,1,40]}");
    s.clock.trust = SH_CLOCK_TZ_UNCONFIRMED;
    HAS(&s, "\"st\":\"tz\"");
    s.clock.trust = SH_CLOCK_UNRELIABLE;
    HAS(&s, "\"st\":\"bad\"");
    s.clock.trust = SH_CLOCK_UNSET;         /* not set: no time */
    HAS(&s, "\"st\":\"unset\",\"sntp\":1,\"min\":null,");
}

static void test_status_buffer(void)
{
    /* the longest status fits the buffer the firmware uses */
    sh_web_status_t s = status();
    s.t_col = -1234;
    s.t_tank = -1234;
    s.reason = SH_PUMP_REASON_BOTH_FAULT;
    s.alarms = 0x7F;
    s.test_on = true;
    s.test_left_s = 900;
    s.cfg.settings = (sh_settings_t){ .v = { 85, 80, 20, 15, 900, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };
    s.cfg.inst = (sh_installer_t){ .v = { SH_FLUID_GLYCOL, 60, 60, 250, SH_COL_FLAT, 20, 150, 120 } };
    s.col_protect = 1600;
    s.name = "0123456789012345678901234567890";
    s.version = "0123456789012345678901234567890";
    s.ssid = "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"";     /* 32 escaped characters */
    s.ip = "255.255.255.255";
    s.rssi = -100;
    s.clock = (sh_web_clock_t){ .trust = SH_CLOCK_UNRELIABLE, .sntp = true, .min = 1439,
                                .tz = "0123456789012345678901234567890", .tz_confirmed = true, .peak = 1439 };
    s.run_s[0] = s.run_s[1] = s.run_s[2] = s.run_s[3] = 86400;
    s.history = true;
    s.winter = true;
    s.cool = SH_COOL_E4;
    s.cool_target = -1234;
    s.cool_until = 1439;
    s.cool_heater = true;
    s.cool_learned = -1234;
    s.sunrise = s.sunset = 1439;
    s.air_on = s.air_run = true;
    s.air_cycle = 3;
    s.air_left_s = 99999;
    s.heater_on = s.heater_det = true;
    s.leak = SH_LEAK_STOP;
    s.leak_left_s = 300;
    s.leak_last = s.leak_last_stop = true;
    s.leak_last_drop = -1234;
    s.e8_normal = -1234;
    s.alarms = 0xFF;
    s.reason = SH_PUMP_REASON_PULSE_WAIT;
    s.cfg.inst.v[SH_INST_MOUNT] = SH_MOUNT_PIPES;
    s.pulse = s.meas = s.meas_pulse = s.meas_now = s.pulse_next = s.pulse_hot = true;
    s.meas_age_s = 999999;
    s.pulse_left_s = 99999;
    s.ap = s.ap_pending = true;
    s.host = s.ap_ssid = "01234567890123456789012345678901";  /* 32 characters */
    s.ota = SH_OTA_RUNNING;
    s.ota_pct = 100;
    s.ota_version = "0123456789012345678901234567890";
    const int len = sh_web_status_json(s_json, sizeof(s_json), &s);
    CHECK(len > 0 && len < SH_WEB_STATUS_MAX * 3 / 4);

    /* a buffer that is too small: -1, nothing written past it */
    char small[64];
    memset(small, 'x', sizeof(small));
    CHECK_EQ(sh_web_status_json(small, 32, &s), -1);
    CHECK_EQ(small[32], 'x');
    CHECK_EQ(sh_web_status_json(small, 0, &s), -1);
    CHECK_EQ(sh_web_status_json(s_json, (size_t)len, &s), -1);          /* no room for the NUL */
    CHECK_EQ(sh_web_status_json(s_json, (size_t)len + 1, &s), len);
    /* the pulse mode (group D2): the age of the values, the next pulse, the reasons, the mounting */
    sh_web_status_t u = status();
    u.pulse = true;
    u.meas = true;
    u.meas_age_s = 125;
    u.meas_pulse = true;
    u.pulse_next = true;
    u.pulse_left_s = 1675;
    u.reason = SH_PUMP_REASON_PULSE_WAIT;
    u.pump_on = false;
    HAS(&u, "\"pulse\":{\"on\":1,\"age\":125,\"at\":1,\"now\":0,\"next\":1675,\"hot\":0},");
    HAS(&u, "\"why\":\"pulse_wait\"");
    u.meas = false;
    u.pulse_next = false;
    u.pulse_hot = true;
    u.reason = SH_PUMP_REASON_PULSE;
    HAS(&u, "\"pulse\":{\"on\":1,\"age\":null,\"at\":1,\"now\":0,\"next\":null,\"hot\":1},");
    HAS(&u, "\"why\":\"pulse\"");
    u.cfg.inst.v[SH_INST_MOUNT] = SH_MOUNT_PIPES;
    HAS(&u, "\"mount\":{\"v\":1,\"min\":0,\"max\":1}");
}

/* One "set" request on the defaults. */
static sh_web_res_t set(const char *body, sh_web_config_t *c, unsigned *changed)
{
    *c = config();
    *changed = 0;
    return sh_web_set(body, CODE, c, changed);
}

static void test_set_settings(void)
{
    sh_web_config_t c;
    unsigned ch;

    /* every menu value: the ends of its range are accepted, one step outside is not */
    static const struct { const char *ok_min, *ok_max, *low, *high; sh_setting_t id; int16_t min, max; } R[] = {
        { "tbo=30", "tbo=85", "tbo=29", "tbo=86", SH_SET_TANK_MAX, 30, 85 },
        { "c_lo=10", "c_lo=80", "c_lo=9", "c_lo=81", SH_SET_COL_MIN, 10, 80 },
        { "d_on=5", "d_on=20", "d_on=4", "d_on=21", SH_SET_DIFF_ON, 5, 20 },    /* d.oF is 4 */
        { "d_of=1", "d_of=5", "d_of=0", "d_of=6", SH_SET_DIFF_OFF, 1, 5 },      /* d.on is 6 */
        { "t_st=60", "t_st=900", "t_st=30", "t_st=930", SH_SET_TEST_S, 60, 900 },
    };
    for (unsigned i = 0; i < sizeof(R) / sizeof(R[0]); i++) {
        CHECK_EQ(set(R[i].ok_min, &c, &ch), SH_WEB_OK);
        CHECK_EQ(c.settings.v[R[i].id], R[i].min);
        CHECK_EQ(ch, SH_WEB_CH_SETTINGS);
        CHECK_EQ(set(R[i].ok_max, &c, &ch), SH_WEB_OK);
        CHECK_EQ(c.settings.v[R[i].id], R[i].max);
        CHECK_EQ(set(R[i].low, &c, &ch), SH_WEB_BAD);
        CHECK_EQ(c.settings.v[R[i].id], config().settings.v[R[i].id]);     /* nothing changed */
        CHECK_EQ(set(R[i].high, &c, &ch), SH_WEB_BAD);
    }
    CHECK_EQ(set("t_st=310", &c, &ch), SH_WEB_BAD);     /* off the 30 s step, as in the menu */
    CHECK_EQ(set("t_st=330", &c, &ch), SH_WEB_OK);

    /* no access code is needed, a wrong one does not matter */
    CHECK_EQ(set("tbo=60&code=000000", &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.settings.v[SH_SET_TANK_MAX], 60);

    /* several values: in their order, each against the range the others leave it */
    CHECK_EQ(set("d_on=10&d_of=8", &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.settings.v[SH_SET_DIFF_ON], 10);
    CHECK_EQ(c.settings.v[SH_SET_DIFF_OFF], 8);
    CHECK_EQ(set("d_of=8&d_on=10", &c, &ch), SH_WEB_BAD);   /* 8 is not below d.on = 6 */
    CHECK_EQ(c.settings.v[SH_SET_DIFF_OFF], 4);             /* all or nothing */
    CHECK_EQ(set("tbo=60&c_lo=5", &c, &ch), SH_WEB_BAD);
    CHECK_EQ(c.settings.v[SH_SET_TANK_MAX], 70);

    /* the frost protection switch */
    CHECK_EQ(set("fro=0", &c, &ch), SH_WEB_OK);
    CHECK(!c.frost);
    CHECK_EQ(ch, SH_WEB_CH_FROST);
    CHECK_EQ(set("fro=1", &c, &ch), SH_WEB_OK);
    CHECK(c.frost);
    CHECK_EQ(set("fro=2", &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("fro=0&tbo=65", &c, &ch), SH_WEB_OK);
    CHECK_EQ(ch, SH_WEB_CH_FROST | SH_WEB_CH_SETTINGS);
}

static void test_set_installer(void)
{
    sh_web_config_t c;
    unsigned ch;

    /* only with the access code (any letter case) */
    CHECK_EQ(set("press=22", &c, &ch), SH_WEB_DENIED);
    CHECK_EQ(c.inst.v[SH_INST_PRESSURE], 15);
    CHECK_EQ(set("press=22&code=000000", &c, &ch), SH_WEB_DENIED);
    CHECK_EQ(set("press=22&code=81084", &c, &ch), SH_WEB_DENIED);
    CHECK_EQ(set("press=22&code=81084A1", &c, &ch), SH_WEB_DENIED);
    CHECK_EQ(set("press=22&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_PRESSURE], 22);
    CHECK_EQ(ch, SH_WEB_CH_INSTALLER);
    CHECK_EQ(set("code=81084a&press=22", &c, &ch), SH_WEB_OK);
    /* a free setting in the same request does not get through without the code either */
    CHECK_EQ(set("tbo=60&press=22", &c, &ch), SH_WEB_DENIED);
    CHECK_EQ(c.settings.v[SH_SET_TANK_MAX], 70);

    /* the ranges */
    CHECK_EQ(set("press=5&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(set("press=60&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(set("press=4&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("press=61&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("col_max=100&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(set("col_max=250&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_COL_MAX], 250);
    CHECK_EQ(set("col_max=99&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("col_max=251&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("fluid=2&code=" CODE, &c, &ch), SH_WEB_BAD);

    /* glycol: with its percentage; water takes it back to 0 */
    CHECK_EQ(set("fluid=1&glycol=30&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_FLUID], SH_FLUID_GLYCOL);
    CHECK_EQ(c.inst.v[SH_INST_GLYCOL], 30);
    CHECK_EQ(set("fluid=1&glycol=61&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("glycol=30&code=" CODE, &c, &ch), SH_WEB_BAD);     /* the fluid is water */
    CHECK_EQ(set("fluid=0&glycol=30&code=" CODE, &c, &ch), SH_WEB_BAD);
    c.inst = (sh_installer_t){ .v = { SH_FLUID_GLYCOL, 40, 22, 150, SH_COL_FLAT, 20, 150 } };
    CHECK_EQ(sh_web_set("glycol=45&code=" CODE, CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_GLYCOL], 45);
    CHECK_EQ(sh_web_set("fluid=0&code=" CODE, CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_FLUID], SH_FLUID_WATER);
    CHECK_EQ(c.inst.v[SH_INST_GLYCOL], 0);
    CHECK_EQ(c.inst.v[SH_INST_PRESSURE], 22);                       /* the others stay */
}

/* Group C (doc/tasks/step10-cooling-vacation-seasons.md 3.1, 3.3 - 3.6): the season, the vacation,
 * the winter set, the night cooling switches — free; the collector and the tank — with the code. */
static void test_set_group_c(void)
{
    sh_web_config_t c;
    unsigned ch;

    CHECK_EQ(set("hol=1", &c, &ch), SH_WEB_OK);                 /* vacation: no code */
    CHECK_EQ(c.settings.v[SH_SET_VACATION], 1);
    CHECK_EQ(ch, SH_WEB_CH_SETTINGS);
    CHECK_EQ(set("hol=2", &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("ssn=0", &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.settings.v[SH_SET_SEASON], SH_SEASON_SUMMER);
    CHECK_EQ(set("ssn=1", &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.settings.v[SH_SET_SEASON], SH_SEASON_WINTER);
    CHECK_EQ(set("ssn=3", &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("nc=0&nc_w=1", &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.settings.v[SH_SET_NCOOL], 0);
    CHECK_EQ(c.settings.v[SH_SET_NCOOL_W], 1);
    CHECK_EQ(set("nc=2", &c, &ch), SH_WEB_BAD);

    /* the winter set: its own pair of differences (5 / 3) */
    CHECK_EQ(set("c_lo_w=10", &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.settings.v[SH_SET_COL_MIN_W], 10);
    CHECK_EQ(c.settings.v[SH_SET_COL_MIN], 25);                 /* the summer value stays */
    CHECK_EQ(set("c_lo_w=9", &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("d_on_w=4", &c, &ch), SH_WEB_OK);
    CHECK_EQ(set("d_on_w=3", &c, &ch), SH_WEB_BAD);             /* not above the winter d.oF */
    CHECK_EQ(set("d_of_w=4", &c, &ch), SH_WEB_OK);
    CHECK_EQ(set("d_of_w=5", &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("d_on=5", &c, &ch), SH_WEB_OK);                /* summer d.oF is 4: not touched by winter */

    /* collector kind and area, tank volume: with the code, in their ranges and steps */
    CHECK_EQ(set("col_type=1", &c, &ch), SH_WEB_DENIED);
    CHECK_EQ(set("col_type=1&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_COL_TYPE], SH_COL_TUBES);
    CHECK_EQ(ch, SH_WEB_CH_INSTALLER);
    CHECK_EQ(set("col_type=2&code=" CODE, &c, &ch), SH_WEB_BAD);
    /* the mounting of the sensors (group D2): with the code, 0 / 1 */
    CHECK_EQ(set("mount=1", &c, &ch), SH_WEB_DENIED);
    CHECK_EQ(set("mount=1&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_MOUNT], SH_MOUNT_PIPES);
    CHECK(ch & SH_WEB_CH_INSTALLER);
    CHECK_EQ(set("mount=2&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("mount=0&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_MOUNT], SH_MOUNT_SLEEVES);
    CHECK_EQ(set("col_area=5&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(set("col_area=200&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_COL_AREA], 200);
    CHECK_EQ(set("col_area=4&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("col_area=201&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("tank_vol=50&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(set("tank_vol=1000&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_TANK_VOL], 1000);
    CHECK_EQ(set("tank_vol=40&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("tank_vol=1010&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("tank_vol=125&code=" CODE, &c, &ch), SH_WEB_BAD);     /* off the 10 l step */

    /* the heater (group D): 0 = none, 0.5 ... 12.0 kW in steps of 0.5, with the code */
    CHECK_EQ(set("heater=35", &c, &ch), SH_WEB_DENIED);
    CHECK_EQ(set("heater=35&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_HEATER], 35);
    CHECK_EQ(set("heater=0&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_HEATER], 0);
    CHECK_EQ(set("heater=120&code=" CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(set("heater=3&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("heater=37&code=" CODE, &c, &ch), SH_WEB_BAD);
    CHECK_EQ(set("heater=125&code=" CODE, &c, &ch), SH_WEB_BAD);
}

/* The heater, its leak and E8 (group D). */
static void test_status_group_d(void)
{
    sh_web_status_t s = status();
    HAS(&s, "\"heat\":{\"on\":0,\"det\":0,\"leak\":0,\"left\":0,\"last\":null,\"e8n\":null},");
    s.heater_on = true;
    s.heater_det = true;
    s.leak = SH_LEAK_CHECK;
    s.leak_left_s = 120;
    s.leak_last = true;
    s.leak_last_drop = 19;
    s.e8_normal = 6;
    HAS(&s, "\"heat\":{\"on\":1,\"det\":1,\"leak\":1,\"left\":120,\"last\":{\"stop\":0,\"drop\":19},\"e8n\":6},");
    s.leak = SH_LEAK_STOP;
    s.leak_last_stop = true;
    s.leak_last_drop = 23;
    HAS(&s, "\"leak\":2,\"left\":120,\"last\":{\"stop\":1,\"drop\":23}");
    /* the reasons */
    s.pump_on = false;
    s.reason = SH_PUMP_REASON_LEAK_CHECK;
    HAS(&s, "\"why\":\"leak_check\"");
    s.reason = SH_PUMP_REASON_LEAK_STOP;
    HAS(&s, "\"why\":\"leak_stop\"");
    /* E8: after E6, before the information */
    s.alarms = SH_ALARM_BIT(SH_ALARM_HOT) | SH_ALARM_BIT(SH_ALARM_E8) | SH_ALARM_BIT(SH_ALARM_E6);
    HAS(&s, "\"alarms\":[\"E6\",\"E8\",\"Hot\"]");
}

/* The status of the season, the night cooling and the air venting. */
static void test_status_group_c(void)
{
    sh_web_status_t s = status();
    s.winter = true;
    HAS(&s, "\"winter\":1,");

    /* a session after an E4: its target and until when */
    s.cool = SH_COOL_E4;
    s.cool_target = 628;
    s.cool_until = SH_COOL_TO_MIN;
    s.sunrise = 372;
    s.sunset = 1215;
    HAS(&s, "\"cool\":{\"on\":2,\"target\":628,\"until\":420,\"heater\":0,\"learned\":663,\"rise\":372,"
            "\"set\":1215},");
    s.cool = SH_COOL_TMAX;
    s.cool_until = -1;                                          /* no clock: until the sunrise */
    HAS(&s, "\"cool\":{\"on\":1,\"target\":628,\"until\":null,");
    s.cool = SH_COOL_VACATION;                                  /* no target: until the temperatures are equal */
    HAS(&s, "\"cool\":{\"on\":3,\"target\":null,\"until\":null,");
    s.cool = SH_COOL_NONE;
    s.cool_heater = true;                                       /* the information about the heater */
    HAS(&s, "\"cool\":{\"on\":0,\"target\":null,\"until\":null,\"heater\":1,");

    /* air venting: the cycles by the fluid, the phase and the time left */
    s = status();
    HAS(&s, "\"air\":{\"on\":0,\"run\":0,\"cycle\":0,\"left\":0,\"cycles\":3,\"work\":5,\"pause\":10},");
    s.cfg.inst.v[SH_INST_FLUID] = SH_FLUID_GLYCOL;
    s.cfg.inst.v[SH_INST_GLYCOL] = 30;
    s.air_on = true;
    s.air_cycle = 2;
    s.air_left_s = 3000;
    HAS(&s, "\"air\":{\"on\":1,\"run\":0,\"cycle\":2,\"left\":3000,\"cycles\":3,\"work\":5,\"pause\":25},");

    /* the vacation limit with the installer data: 75 % of 150 °C, at most the boiling point - 10 K */
    s.cfg.inst.v[SH_INST_COL_MAX] = 150;
    HAS(&s, "\"vac\":1125}");
    s.cfg.inst.v[SH_INST_COL_MAX] = 250;
    s.cfg.inst.v[SH_INST_GLYCOL] = 0;
    HAS(&s, "\"boil\":1277,");
    HAS(&s, "\"vac\":1177}");

    /* the reasons of group C have their own keys */
    CHECK(strcmp(sh_web_reason_key(SH_PUMP_REASON_COOL), "cool") == 0);
    CHECK(strcmp(sh_web_reason_key(SH_PUMP_REASON_VACATION), "vacation") == 0);
    CHECK(strcmp(sh_web_reason_key(SH_PUMP_REASON_AIR_RUN), "air_run") == 0);
    CHECK(strcmp(sh_web_reason_key(SH_PUMP_REASON_AIR_PAUSE), "air_pause") == 0);
}

static void test_air(void)
{
    sh_web_air_t cmd;
    CHECK_EQ(sh_web_air("cmd=start", false, false, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_AIR_START);
    CHECK_EQ(sh_web_air("cmd=start", true, false, &cmd), SH_WEB_OK);    /* already running */
    CHECK_EQ(cmd, SH_WEB_AIR_NONE);
    CHECK_EQ(sh_web_air("cmd=start", false, true, &cmd), SH_WEB_OK);    /* not during the pump test */
    CHECK_EQ(cmd, SH_WEB_AIR_NONE);
    CHECK_EQ(sh_web_air("cmd=stop", true, false, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_AIR_STOP);
    CHECK_EQ(sh_web_air("cmd=stop", false, false, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_AIR_NONE);
    CHECK_EQ(sh_web_air("cmd=go", false, false, &cmd), SH_WEB_BAD);
    CHECK_EQ(sh_web_air("", false, false, &cmd), SH_WEB_BAD);
    CHECK_EQ(sh_web_air("cmd=start&code=000000", false, false, &cmd), SH_WEB_OK);   /* no code needed */
}

static void test_set_malformed(void)
{
    sh_web_config_t c;
    unsigned ch;
    static const char *const BAD[] = {
        "", "tbo", "tbo=", "=60", "tbo=-", "tbo=6x", "tbo=60.5", "tbo= 60", "tbo=%36%30", "tbo=6+0",
        "tbo=99999", "nope=1", "tbo=60&nope=1", "code=" CODE, "tbo=60&&c_lo=30",
        "tbo=1234567890123456", "a_key_that_is_far_too_long=1",
    };
    for (unsigned i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        CHECK_EQ(set(BAD[i], &c, &ch), SH_WEB_BAD);
        const sh_web_config_t def = config();
        CHECK(memcmp(&c.settings, &def.settings, sizeof(c.settings)) == 0);
    }
}

static void test_code(void)
{
    CHECK(sh_web_code_ok("code=" CODE, CODE));
    CHECK(sh_web_code_ok("what=wifi&code=81084a", CODE));
    CHECK(!sh_web_code_ok("code=81084B", CODE));
    CHECK(!sh_web_code_ok("code=", CODE));
    CHECK(!sh_web_code_ok("what=wifi", CODE));
    CHECK(!sh_web_code_ok("", CODE));
    CHECK(!sh_web_code_ok("code=" CODE, ""));           /* no device code: nothing passes */
    CHECK(!sh_web_code_ok("code=", ""));
    CHECK(!sh_web_code_ok("code=" CODE, NULL));
}

static void test_pump_test(void)
{
    sh_web_test_t cmd;
    /* as the buttons: "+" starts the test or adds 60 s, "-" and "menu" act only during the test */
    CHECK_EQ(sh_web_test("cmd=start", false, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_TEST_PLUS);
    CHECK_EQ(sh_web_test("cmd=start", true, &cmd), SH_WEB_OK);      /* already running: not +60 s */
    CHECK_EQ(cmd, SH_WEB_TEST_NONE);
    CHECK_EQ(sh_web_test("cmd=plus", true, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_TEST_PLUS);
    CHECK_EQ(sh_web_test("cmd=plus", false, &cmd), SH_WEB_OK);      /* ended meanwhile: no new test */
    CHECK_EQ(cmd, SH_WEB_TEST_NONE);
    CHECK_EQ(sh_web_test("cmd=minus", true, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_TEST_MINUS);
    CHECK_EQ(sh_web_test("cmd=minus", false, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_TEST_NONE);
    CHECK_EQ(sh_web_test("cmd=stop", true, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_TEST_STOP);
    CHECK_EQ(sh_web_test("cmd=stop", false, &cmd), SH_WEB_OK);
    CHECK_EQ(cmd, SH_WEB_TEST_NONE);
    CHECK_EQ(sh_web_test("cmd=run", true, &cmd), SH_WEB_BAD);
    CHECK_EQ(sh_web_test("", true, &cmd), SH_WEB_BAD);
    CHECK_EQ(sh_web_test("x=start", true, &cmd), SH_WEB_BAD);
}

static void test_reset(void)
{
    sh_web_reset_t what;
    CHECK_EQ(sh_web_reset("what=wifi&code=" CODE, CODE, &what), SH_WEB_OK);
    CHECK_EQ(what, SH_WEB_RESET_WIFI);
    CHECK_EQ(sh_web_reset("what=defaults&code=" CODE, CODE, &what), SH_WEB_OK);
    CHECK_EQ(what, SH_WEB_RESET_DEFAULTS);
    CHECK_EQ(sh_web_reset("code=" CODE "&what=factory", CODE, &what), SH_WEB_OK);
    CHECK_EQ(what, SH_WEB_RESET_FACTORY);
    /* never without the access code */
    CHECK_EQ(sh_web_reset("what=factory", CODE, &what), SH_WEB_DENIED);
    CHECK_EQ(sh_web_reset("what=wifi&code=123456", CODE, &what), SH_WEB_DENIED);
    CHECK_EQ(sh_web_reset("what=history&code=" CODE, CODE, &what), SH_WEB_OK);
    CHECK_EQ(what, SH_WEB_RESET_HISTORY);
    CHECK_EQ(sh_web_reset("what=learned&code=" CODE, CODE, &what), SH_WEB_OK);
    CHECK_EQ(what, SH_WEB_RESET_LEARNED);
    CHECK_EQ(sh_web_reset("what=history", CODE, &what), SH_WEB_DENIED);
    CHECK_EQ(sh_web_reset("what=learned&code=000000", CODE, &what), SH_WEB_DENIED);
    CHECK_EQ(sh_web_reset("what=homekit&code=" CODE, CODE, &what), SH_WEB_BAD);
    CHECK_EQ(sh_web_reset("code=" CODE, CODE, &what), SH_WEB_BAD);
}

/* ---- step 9: chart, days, time zones, browser time ------------------------------------- */

static char s_big[SH_WEB_CHART_MAX];

static void test_chart(void)
{
    sh_day_t d;
    sh_day_init(&d);
    sh_day_sum_t done;
    sh_local_t lt = { .set = true, .day = 2500, .min = 0 };
    const sh_day_in_t warm = { .t_col = 600, .t_tank = 400, .pump_on = true, .reason = SH_PUMP_REASON_DIFF };
    const sh_day_in_t prot = { .t_col = 1150, .t_tank = 600, .pump_on = true, .reason = SH_PUMP_REASON_COL_PROTECT,
                               .tank_fault = true };
    /* 00:00 - 00:20: charging, then 00:20 - 00:25 protection with a faulty tank sensor (steps of 1 min) */
    uint32_t t = 1000;
    for (; t <= 1000 + 1500; t += 60) {
        lt.min = (uint16_t)((t - 1000) / 60);
        sh_day_step(&d, t < 1000 + 1200 ? &warm : &prot, &lt, t, &done);
    }
    int len = sh_web_chart_json(s_big, sizeof(s_big), &d, &lt, t);
    CHECK(len > 0);
    /* (the temperatures at the end: taken at the first step of the next interval) */
    CHECK(strcmp(s_big, "{\"day\":1,\"pts\":[[10,600,400,10,0,0,0],[20,1150,null,10,0,0,0]]}") == 0);

    /* without a clock: from now, in minutes */
    sh_day_init(&d);
    const sh_local_t none = { .set = false };
    for (t = 0; t <= 1800; t += 60) {
        sh_day_step(&d, &warm, &none, t, &done);
    }
    len = sh_web_chart_json(s_big, sizeof(s_big), &d, &none, 1830);
    CHECK(strcmp(s_big, "{\"day\":0,\"pts\":[[-20,600,400,10,0,0,0],[-10,600,400,10,0,0,0],[0,600,400,10,0,0,0]]}") == 0);

    /* night cooling: the 7th number of a point */
    sh_day_init(&d);
    const sh_day_in_t cool = { .t_col = 200, .t_tank = 700, .pump_on = true, .reason = SH_PUMP_REASON_COOL };
    for (t = 0; t <= 1200; t += 60) {
        sh_day_step(&d, &cool, &none, t, &done);
    }
    len = sh_web_chart_json(s_big, sizeof(s_big), &d, &none, 1230);
    CHECK(strcmp(s_big, "{\"day\":0,\"pts\":[[-10,200,700,0,0,0,10],[0,200,700,0,0,0,10]]}") == 0);

    /* a full chart fits the buffer of the firmware */
    sh_day_init(&d);
    const sh_day_in_t worst = { .t_col = -1234, .t_tank = -1234, .pump_on = true, .reason = SH_PUMP_REASON_DIFF };
    for (t = 0; t <= 2 * 86400; t += 60) {
        sh_day_step(&d, &worst, &none, t, &done);
    }
    len = sh_web_chart_json(s_big, sizeof(s_big), &d, &none, 2 * 86400);
    CHECK(len > 0 && len < SH_WEB_CHART_MAX * 3 / 4);
    CHECK_EQ(sh_web_chart_json(s_big, 40, &d, &none, 2 * 86400), -1);
}

static void test_days(void)
{
    sh_hist_rec_t r[SH_WEB_DAYS];
    sh_hist_rec_init(&r[0], sh_day_number(2026, 11, 11));
    r[0].col_max = 853;
    r[0].col_min = -52;
    r[0].tank_max = 611;
    r[0].tank_min = 402;
    r[0].run_min[0] = 190;
    r[0].run_min[1] = 4;
    r[0].cool_min = 75;
    r[0].heater_min = 95;
    r[0].alarms = SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_HOT);
    r[0].peak_min = 790;
    r[0].clock_reliable = true;
    sh_hist_rec_init(&r[1], sh_day_number(2026, 11, 10));
    r[1].partial = true;
    char buf[SH_WEB_DAYS_MAX];
    CHECK(sh_web_days_json(buf, sizeof(buf), true, r, 2) > 0);
    CHECK(strcmp(buf, "{\"on\":1,\"days\":["
                      "{\"d\":\"2026-11-11\",\"cx\":853,\"cn\":-52,\"tx\":611,\"tn\":402,\"run\":[190,4,0],"
                      "\"cool\":75,\"heat\":95,\"al\":[\"E1\",\"Hot\"],\"pk\":790,\"rel\":1,\"part\":0},"
                      "{\"d\":\"2026-11-10\",\"cx\":null,\"cn\":null,\"tx\":null,\"tn\":null,\"run\":[0,0,0],"
                      "\"cool\":0,\"heat\":0,\"al\":[],\"pk\":null,\"rel\":0,\"part\":1}]}") == 0);
    CHECK(sh_web_days_json(buf, sizeof(buf), false, r, 0) > 0);
    CHECK(strcmp(buf, "{\"on\":0,\"days\":[]}") == 0);

    /* 7 days of the longest kind fit */
    for (int i = 0; i < SH_WEB_DAYS; i++) {
        sh_hist_rec_init(&r[i], 0xFFFE);
        r[i].col_max = r[i].col_min = r[i].tank_max = r[i].tank_min = -1234;
        r[i].run_min[0] = r[i].run_min[1] = r[i].run_min[2] = r[i].cool_min = r[i].heater_min = 65535;
        r[i].alarms = 0xFF;
        r[i].peak_min = 1439;
    }
    const int len = sh_web_days_json(buf, sizeof(buf), true, r, SH_WEB_DAYS);
    CHECK(len > 0 && len < SH_WEB_DAYS_MAX * 3 / 4);
}

static void test_tz(void)
{
    char buf[SH_WEB_TZ_MAX];
    const int len = sh_web_tz_json(buf, sizeof(buf));
    CHECK(len > 0 && len < SH_WEB_TZ_MAX * 3 / 4);
    static const char start[] = "{\"tz\":[\"UTC\",\"Europe/Sofia\",";
    CHECK(strncmp(buf, start, sizeof(start) - 1) == 0);

    /* the installer confirms a zone by its index, with the code */
    int tz = -1;
    CHECK_EQ(sh_web_tz("tz=1&code=" CODE, CODE, &tz), SH_WEB_OK);
    CHECK_EQ(tz, 1);
    CHECK_EQ(sh_web_tz("tz=1", CODE, &tz), SH_WEB_DENIED);
    CHECK_EQ(sh_web_tz("tz=-1&code=" CODE, CODE, &tz), SH_WEB_BAD);
    char body[64];
    snprintf(body, sizeof(body), "tz=%d&code=%s", (int)sh_tz_count, CODE);
    CHECK_EQ(sh_web_tz(body, CODE, &tz), SH_WEB_BAD);
    snprintf(body, sizeof(body), "tz=%d&code=%s", (int)sh_tz_count - 1, CODE);
    CHECK_EQ(sh_web_tz(body, CODE, &tz), SH_WEB_OK);
    CHECK_EQ(sh_web_tz("tz=x&code=" CODE, CODE, &tz), SH_WEB_BAD);
}

static void test_time(void)
{
    /* the time of the browser needs no code; only digits */
    int64_t t = 0;
    CHECK_EQ(sh_web_time("t=1794470400", &t), SH_WEB_OK);
    CHECK(t == 1794470400LL);
    CHECK_EQ(sh_web_time("t=1794470400&code=" CODE, &t), SH_WEB_OK);
    CHECK_EQ(sh_web_time("t=", &t), SH_WEB_BAD);
    CHECK_EQ(sh_web_time("t=-5", &t), SH_WEB_BAD);
    CHECK_EQ(sh_web_time("t=17944704a0", &t), SH_WEB_BAD);
    CHECK_EQ(sh_web_time("t=1234567890123", &t), SH_WEB_BAD);
    CHECK_EQ(sh_web_time("x=1", &t), SH_WEB_BAD);
}

int main(void)
{
    test_status();
    test_status_values();
    test_status_buffer();
    test_set_settings();
    test_set_installer();
    test_set_malformed();
    test_set_group_c();
    test_status_group_c();
    test_status_group_d();
    test_air();
    test_code();
    test_pump_test();
    test_reset();
    test_chart();
    test_days();
    test_tz();
    test_time();
    return test_summary("test_web");
}
