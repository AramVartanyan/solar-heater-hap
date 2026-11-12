/* Host test of the web interface data and request checks (main/sh_web_api.c;
 * doc/tasks/step6-installer-web.md 3.4): the status JSON, the settings ("set") with the checks of
 * the menu, the access code, the pump test commands, the resets. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_web_api.h"

#define CODE    "81084A"

/* The defaults of Kconfig */
static sh_web_config_t config(void)
{
    const sh_web_config_t c = {
        .settings = { .v = { 70, 25, 6, 4, 300 } },
        .frost = true,
        .inst = { .v = { SH_FLUID_WATER, 0, 15, 120 } },
    };
    return c;
}

static sh_web_status_t status(void)
{
    const sh_web_status_t s = {
        .t_col = 523, .t_tank = 418, .pump_on = true, .reason = SH_PUMP_REASON_DIFF,
        .cfg = config(), .col_protect = 1100,
        .name = "solar-heater", .version = "0.1.0", .ssid = "Home", .ip = "192.168.1.7", .rssi = -41,
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
        "{\"name\":\"solar-heater\",\"fw\":\"0.1.0\","
        "\"feat\":[\"alarms\",\"temps\",\"pump\",\"pump_test\",\"settings\",\"frost\",\"fluid\","
        "\"pressure\",\"col_max\",\"calc\",\"net\",\"fw\",\"reset\"],"
        "\"tank\":418,\"col\":523,\"pump\":1,\"why\":\"diff\",\"alarms\":[],\"test\":0,\"left\":0,"
        "\"set\":{\"tbo\":{\"v\":70,\"min\":30,\"max\":85,\"step\":1},"
        "\"c_lo\":{\"v\":25,\"min\":10,\"max\":80,\"step\":1},"
        "\"d_on\":{\"v\":6,\"min\":5,\"max\":20,\"step\":1},"
        "\"d_of\":{\"v\":4,\"min\":1,\"max\":5,\"step\":1},"
        "\"t_st\":{\"v\":300,\"min\":60,\"max\":900,\"step\":30}},\"fro\":1,"
        "\"inst\":{\"fluid\":{\"v\":0,\"min\":0,\"max\":1},\"glycol\":{\"v\":0,\"min\":0,\"max\":60},"
        "\"press\":{\"v\":15,\"min\":5,\"max\":60},\"col_max\":{\"v\":120,\"min\":100,\"max\":250}},"
        "\"calc\":{\"boil\":1277,\"cap\":1150,\"prot\":1100,\"prot_set\":1100,\"frost\":40},"
        "\"net\":{\"ssid\":\"Home\",\"ip\":\"192.168.1.7\",\"rssi\":-41}}") == 0);
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
    s.cfg.inst = (sh_installer_t){ .v = { SH_FLUID_GLYCOL, 9, 22, 150 } };
    s.col_protect = 1400;
    HAS(&s, "\"inst\":{\"fluid\":{\"v\":1,\"min\":0,\"max\":1},\"glycol\":{\"v\":9,");
    HAS(&s, "\"calc\":{\"boil\":1365,\"cap\":1265,\"prot\":1265,\"prot_set\":1400,\"frost\":13}");

    /* text values are escaped; no network -> empty strings */
    s = status();
    s.ssid = "a\"b\\c\td";
    HAS(&s, "\"ssid\":\"a\\\"b\\\\c\\u0009d\"");
    s.ssid = "\xD0\x94\xD0\xBE\xD0\xBC";       /* UTF-8 passes */
    HAS(&s, "\"ssid\":\"\xD0\x94\xD0\xBE\xD0\xBC\"");
    s.ssid = "";
    s.ip = NULL;
    s.rssi = 0;
    HAS(&s, "\"net\":{\"ssid\":\"\",\"ip\":\"\",\"rssi\":0}}");

    /* a missing function is not in the list (the page greys it out) */
    s = status();
    HAS(&s, "\"frost\"");
    HAS_NOT(&s, "\"vacation\"");
    HAS_NOT(&s, "\"ota\"");
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
    s.cfg.settings = (sh_settings_t){ .v = { 85, 80, 20, 15, 900 } };
    s.cfg.inst = (sh_installer_t){ .v = { SH_FLUID_GLYCOL, 60, 60, 250 } };
    s.col_protect = 1600;
    s.name = "0123456789012345678901234567890";
    s.version = "0123456789012345678901234567890";
    s.ssid = "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"";     /* 32 escaped characters */
    s.ip = "255.255.255.255";
    s.rssi = -100;
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
    c.inst = (sh_installer_t){ .v = { SH_FLUID_GLYCOL, 40, 22, 150 } };
    CHECK_EQ(sh_web_set("glycol=45&code=" CODE, CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_GLYCOL], 45);
    CHECK_EQ(sh_web_set("fluid=0&code=" CODE, CODE, &c, &ch), SH_WEB_OK);
    CHECK_EQ(c.inst.v[SH_INST_FLUID], SH_FLUID_WATER);
    CHECK_EQ(c.inst.v[SH_INST_GLYCOL], 0);
    CHECK_EQ(c.inst.v[SH_INST_PRESSURE], 22);                       /* the others stay */
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
    CHECK_EQ(sh_web_reset("what=homekit&code=" CODE, CODE, &what), SH_WEB_BAD);
    CHECK_EQ(sh_web_reset("code=" CODE, CODE, &what), SH_WEB_BAD);
}

int main(void)
{
    test_status();
    test_status_values();
    test_status_buffer();
    test_set_settings();
    test_set_installer();
    test_set_malformed();
    test_code();
    test_pump_test();
    test_reset();
    return test_summary("test_web");
}
