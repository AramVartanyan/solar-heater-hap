/* Host test of the web interface data (main/sh_web_data.c; doc/tasks/step6-installer-web.md 3.4):
 * the JSON of the state, the parameters and commands of the requests, the access code. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_web_data.h"

#define HAS(json, text)     CHECK(strstr((json), (text)) != NULL)
#define HASNT(json, text)   CHECK(strstr((json), (text)) == NULL)

static sh_web_snap_t snap(void)
{
    sh_web_snap_t s = {
        .t_col = 523, .t_tank = -15,
        .pump_on = true, .reason = SH_PUMP_REASON_DIFF,
        .settings = { .v = { 70, 25, 6, 4, 300 } },
        .frost_on = true,
        .inst = { .v = { SH_COOLANT_GLYCOL, 9, 22, 150 } },
        .version = "0.2.0", .host = "solar-heater", .ssid = "A16blue", .ip = "192.168.1.20",
        .rssi = -41,
    };
    sh_installer_limits(&s.inst, &s.limits);
    s.col_protect = 1100;
    s.col_protect_used = 1100;
    return s;
}

static void test_json_state(void)
{
    char buf[1536];
    sh_web_snap_t s = snap();
    const size_t n = sh_web_state_json(&s, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK_EQ(n, strlen(buf));
    CHECK(buf[0] == '{' && buf[n - 1] == '}');
    HAS(buf, "\"f\":[\"alarms\",\"temps\",");
    HAS(buf, "\"reset_factory\"]");
    HAS(buf, "\"ver\":\"0.2.0\"");
    HAS(buf, "\"host\":\"solar-heater\"");
    HAS(buf, "\"al\":[]");
    HAS(buf, "\"tc\":523,\"tt\":-15,\"cf\":0,\"tf\":0,\"pump\":1,\"why\":\"diff\",\"test\":null");
    HAS(buf, "\"set\":{\"tbo\":70,\"c_lo\":25,\"d_on\":6,\"d_of\":4,\"t_st\":300,\"frost\":1}");
    /* ranges: d.on at least d.oF + 1, d.oF at most d.on - 1; the installer data */
    HAS(buf, "\"rng\":{\"tbo\":[30,85,1],\"c_lo\":[10,80,1],\"d_on\":[5,20,1],\"d_of\":[1,5,1],"
             "\"t_st\":[60,900,30],\"coolant\":[0,1,1],\"glycol\":[0,60,1],\"p_min\":[5,60,1],"
             "\"col_max\":[100,250,1]}");
    HAS(buf, "\"inst\":{\"coolant\":1,\"glycol\":9,\"p_min\":22,\"col_max\":150}");
    /* ~9 % PG, 2.2 bar, collector 150 °C: boiling 136.5, cap 126.5, frost below 1.3 °C */
    HAS(buf, "\"calc\":{\"boil\":1365,\"cap\":1265,\"prot\":1100,\"used\":1100,\"ban\":1365,\"frost\":13}");
    HAS(buf, "\"net\":{\"ssid\":\"A16blue\",\"ip\":\"192.168.1.20\",\"rssi\":-41}");
}

static void test_json_codes_faults_test(void)
{
    char buf[1536];
    sh_web_snap_t s = snap();
    s.alarms = SH_ALARM_BIT(SH_ALARM_FRO) | SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_HOT);
    s.col_fault = true;                 /* faulty: null, not the last valid value */
    s.t_tank = SH_TEMP_INVALID;         /* no value yet: null */
    s.test_on = true;
    s.test_left_s = 245;
    s.reason = SH_PUMP_REASON_TEST;
    CHECK(sh_web_state_json(&s, buf, sizeof(buf)) > 0);
    HAS(buf, "\"al\":[\"E1\",\"Hot\",\"Fro\"]");   /* alarms first, then the information */
    HAS(buf, "\"tc\":null,\"tt\":null,\"cf\":1,\"tf\":0");
    HAS(buf, "\"why\":\"test\",\"test\":245");
}

static void test_json_escape_and_overflow(void)
{
    char buf[1536];
    sh_web_snap_t s = snap();
    s.ssid = "my \"net\"\\\x01";
    CHECK(sh_web_state_json(&s, buf, sizeof(buf)) > 0);
    HAS(buf, "\"ssid\":\"my \\\"net\\\"\\\\\\u0001\"");

    s = snap();
    const size_t full = sh_web_state_json(&s, buf, sizeof(buf));
    CHECK(full > 0);
    CHECK_EQ(sh_web_state_json(&s, buf, full), 0);          /* no room for the NUL */
    CHECK_EQ(sh_web_state_json(&s, buf, full + 1), full);
    CHECK_EQ(sh_web_state_json(&s, buf, 10), 0);
    CHECK_EQ(sh_web_state_json(&s, buf, 0), 0);
}

static void test_params(void)
{
    sh_web_param_t p;
    CHECK(sh_web_param_find("tbo", &p));
    CHECK_EQ(p.group, SH_WEB_PARAM_SETTING);
    CHECK_EQ(p.id, SH_SET_TANK_MAX);
    CHECK(!p.code);
    CHECK(sh_web_param_find("t_st", &p));
    CHECK_EQ(p.id, SH_SET_TEST_S);
    CHECK(sh_web_param_find("frost", &p));
    CHECK_EQ(p.group, SH_WEB_PARAM_FROST);
    CHECK(!p.code);
    /* installer data: only with the code */
    CHECK(sh_web_param_find("coolant", &p));
    CHECK_EQ(p.group, SH_WEB_PARAM_INSTALLER);
    CHECK_EQ(p.id, SH_INST_COOLANT);
    CHECK(p.code);
    CHECK(sh_web_param_find("glycol", &p));
    CHECK_EQ(p.id, SH_INST_GLYCOL);
    CHECK(sh_web_param_find("p_min", &p));
    CHECK_EQ(p.id, SH_INST_P_MIN);
    CHECK(p.code);
    CHECK(sh_web_param_find("col_max", &p));
    CHECK_EQ(p.id, SH_INST_COL_MAX);
    CHECK(!sh_web_param_find("", &p));
    CHECK(!sh_web_param_find("tbo2", &p));
    CHECK(!sh_web_param_find(NULL, &p));
}

static void test_cmds(void)
{
    sh_web_cmd_t c;
    bool code;
    CHECK(sh_web_cmd_find("test_plus", &c, &code));
    CHECK_EQ(c, SH_WEB_CMD_TEST_PLUS);
    CHECK(!code);
    CHECK(sh_web_cmd_find("test_minus", &c, &code));
    CHECK_EQ(c, SH_WEB_CMD_TEST_MINUS);
    CHECK(sh_web_cmd_find("test_stop", &c, &code));
    CHECK_EQ(c, SH_WEB_CMD_TEST_STOP);
    CHECK(!code);
    CHECK(sh_web_cmd_find("reset_wifi", &c, &code));
    CHECK_EQ(c, SH_WEB_CMD_RESET_WIFI);
    CHECK(code);
    CHECK(sh_web_cmd_find("reset_settings", &c, &code));
    CHECK_EQ(c, SH_WEB_CMD_RESET_SETTINGS);
    CHECK(code);
    CHECK(sh_web_cmd_find("reset_factory", &c, &code));
    CHECK_EQ(c, SH_WEB_CMD_RESET_FACTORY);
    CHECK(code);
    CHECK(!sh_web_cmd_find("reset_homekit", &c, &code));    /* not yet */
    CHECK(!sh_web_cmd_find(NULL, &c, &code));
}

static void test_parse_int(void)
{
    int16_t v = 7;
    CHECK(sh_web_parse_int("0", &v));
    CHECK_EQ(v, 0);
    CHECK(sh_web_parse_int("85", &v));
    CHECK_EQ(v, 85);
    CHECK(sh_web_parse_int("-40", &v));
    CHECK_EQ(v, -40);
    CHECK(sh_web_parse_int("32767", &v));
    CHECK_EQ(v, 32767);
    CHECK(sh_web_parse_int("-32768", &v));
    CHECK_EQ(v, -32768);
    v = 7;
    CHECK(!sh_web_parse_int("32768", &v));
    CHECK(!sh_web_parse_int("99999999999", &v));
    CHECK(!sh_web_parse_int("", &v));
    CHECK(!sh_web_parse_int("-", &v));
    CHECK(!sh_web_parse_int("2.2", &v));
    CHECK(!sh_web_parse_int("12a", &v));
    CHECK(!sh_web_parse_int(" 1", &v));
    CHECK(!sh_web_parse_int("+1", &v));
    CHECK(!sh_web_parse_int(NULL, &v));
    CHECK_EQ(v, 7);                     /* unchanged on failure */
}

static void test_code(void)
{
    CHECK(sh_web_code_ok("810841", "SOLAR-810841"));
    CHECK(sh_web_code_ok("A1B2C3", "SOLAR-A1B2C3"));
    CHECK(sh_web_code_ok("a1b2c3", "SOLAR-A1B2C3"));    /* case does not matter */
    CHECK(!sh_web_code_ok("810842", "SOLAR-810841"));
    CHECK(!sh_web_code_ok("81084", "SOLAR-810841"));
    CHECK(!sh_web_code_ok("8108411", "SOLAR-810841"));
    CHECK(!sh_web_code_ok("", "SOLAR-810841"));
    CHECK(!sh_web_code_ok("R-8108", "SOLAR-810841"));
    CHECK(!sh_web_code_ok("12345", "12345"));
    CHECK(!sh_web_code_ok(NULL, "SOLAR-810841"));
    CHECK(!sh_web_code_ok("810841", NULL));
}

static void test_reason_keys(void)
{
    /* every reason has its own key (the page has a text for each) */
    for (int a = 0; a < SH_PUMP_REASON_COUNT; a++) {
        const char *ka = sh_web_reason_key((sh_pump_reason_t)a);
        CHECK(a == SH_PUMP_REASON_NONE || strcmp(ka, "none") != 0);
        for (int b = a + 1; b < SH_PUMP_REASON_COUNT; b++) {
            CHECK(strcmp(ka, sh_web_reason_key((sh_pump_reason_t)b)) != 0);
        }
    }
    CHECK(strcmp(sh_web_reason_key(SH_PUMP_REASON_FROST), "frost") == 0);
    CHECK(strcmp(sh_web_reason_key(SH_PUMP_REASON_COUNT), "none") == 0);
}

int main(void)
{
    test_json_state();
    test_json_codes_faults_test();
    test_json_escape_and_overflow();
    test_params();
    test_cmds();
    test_parse_int();
    test_code();
    test_reason_keys();
    return test_summary("test_web");
}
