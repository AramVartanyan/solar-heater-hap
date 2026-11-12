/* Host test of the user settings table and checks (main/sh_settings.c;
 * doc/tasks/step2-settings-menu-pump-sensors.md 3.1). */

#include <string.h>
#include "test_util.h"
#include "sh_settings.h"

static const sh_settings_t DEF = { .v = { 70, 25, 6, 4, 300 } };

static void test_table(void)
{
    CHECK_EQ(sh_setting_info[SH_SET_TANK_MAX].min, 30);
    CHECK_EQ(sh_setting_info[SH_SET_TANK_MAX].max, 85);
    CHECK_EQ(sh_setting_info[SH_SET_COL_MIN].min, 10);
    CHECK_EQ(sh_setting_info[SH_SET_COL_MIN].max, 80);
    CHECK_EQ(sh_setting_info[SH_SET_DIFF_ON].min, 3);
    CHECK_EQ(sh_setting_info[SH_SET_DIFF_ON].max, 20);
    CHECK_EQ(sh_setting_info[SH_SET_DIFF_OFF].min, 1);
    CHECK_EQ(sh_setting_info[SH_SET_DIFF_OFF].max, 15);
    CHECK_EQ(sh_setting_info[SH_SET_TEST_S].min, 60);
    CHECK_EQ(sh_setting_info[SH_SET_TEST_S].max, 900);
    CHECK_EQ(sh_setting_info[SH_SET_TEST_S].step, 30);
    for (int i = 0; i < SH_SET_COUNT; i++) {        /* NVS keys: unique, at most 15 characters */
        CHECK(strlen(sh_setting_info[i].key) > 0 && strlen(sh_setting_info[i].key) <= 15);
        for (int j = 0; j < i; j++) {
            CHECK(strcmp(sh_setting_info[i].key, sh_setting_info[j].key) != 0);
        }
    }
}

static void test_value_ok(void)
{
    CHECK(!sh_settings_value_ok(SH_SET_TANK_MAX, 29));
    CHECK(sh_settings_value_ok(SH_SET_TANK_MAX, 30));
    CHECK(sh_settings_value_ok(SH_SET_TANK_MAX, 85));
    CHECK(!sh_settings_value_ok(SH_SET_TANK_MAX, 86));
    CHECK(!sh_settings_value_ok(SH_SET_COL_MIN, 9));
    CHECK(sh_settings_value_ok(SH_SET_COL_MIN, 10));
    CHECK(sh_settings_value_ok(SH_SET_COL_MIN, 80));
    CHECK(!sh_settings_value_ok(SH_SET_COL_MIN, 81));
    CHECK(!sh_settings_value_ok(SH_SET_DIFF_ON, 2));
    CHECK(sh_settings_value_ok(SH_SET_DIFF_ON, 3));
    CHECK(sh_settings_value_ok(SH_SET_DIFF_ON, 20));
    CHECK(!sh_settings_value_ok(SH_SET_DIFF_ON, 21));
    CHECK(!sh_settings_value_ok(SH_SET_DIFF_OFF, 0));
    CHECK(sh_settings_value_ok(SH_SET_DIFF_OFF, 1));
    CHECK(sh_settings_value_ok(SH_SET_DIFF_OFF, 15));
    CHECK(!sh_settings_value_ok(SH_SET_DIFF_OFF, 16));
    CHECK(!sh_settings_value_ok(SH_SET_TEST_S, 30));
    CHECK(sh_settings_value_ok(SH_SET_TEST_S, 60));
    CHECK(!sh_settings_value_ok(SH_SET_TEST_S, 75));    /* off the 30 s step */
    CHECK(sh_settings_value_ok(SH_SET_TEST_S, 90));
    CHECK(sh_settings_value_ok(SH_SET_TEST_S, 900));
    CHECK(!sh_settings_value_ok(SH_SET_TEST_S, 930));
    CHECK(!sh_settings_value_ok(SH_SET_TEST_S, -1));    /* e.g. an erased value */
}

static void test_range_coupling(void)
{
    sh_settings_t s = DEF;          /* d.on 6, d.oF 4 */
    int16_t min, max;
    sh_settings_range(&s, SH_SET_DIFF_ON, &min, &max);
    CHECK_EQ(min, 5);               /* d.oF + 1 */
    CHECK_EQ(max, 20);
    sh_settings_range(&s, SH_SET_DIFF_OFF, &min, &max);
    CHECK_EQ(min, 1);
    CHECK_EQ(max, 5);               /* d.on - 1 */

    s.v[SH_SET_DIFF_OFF] = 1;       /* the table limit is narrower */
    sh_settings_range(&s, SH_SET_DIFF_ON, &min, &max);
    CHECK_EQ(min, 3);
    s.v[SH_SET_DIFF_ON] = 20;
    sh_settings_range(&s, SH_SET_DIFF_OFF, &min, &max);
    CHECK_EQ(max, 15);

    sh_settings_range(&s, SH_SET_TANK_MAX, &min, &max);  /* the others: the table */
    CHECK_EQ(min, 30);
    CHECK_EQ(max, 85);
}

static void test_step(void)
{
    sh_settings_t s = DEF;
    CHECK_EQ(sh_settings_step(&s, SH_SET_TANK_MAX, 70, 1), 71);
    CHECK_EQ(sh_settings_step(&s, SH_SET_TANK_MAX, 70, -1), 69);
    CHECK_EQ(sh_settings_step(&s, SH_SET_TANK_MAX, 85, 1), 85);     /* no wrap */
    CHECK_EQ(sh_settings_step(&s, SH_SET_TANK_MAX, 30, -1), 30);
    CHECK_EQ(sh_settings_step(&s, SH_SET_TEST_S, 300, 1), 330);
    CHECK_EQ(sh_settings_step(&s, SH_SET_TEST_S, 60, -1), 60);
    CHECK_EQ(sh_settings_step(&s, SH_SET_TEST_S, 900, 1), 900);
    CHECK_EQ(sh_settings_step(&s, SH_SET_DIFF_ON, 5, -1), 5);       /* d.oF 4 + 1 */
    CHECK_EQ(sh_settings_step(&s, SH_SET_DIFF_OFF, 5, 1), 5);       /* d.on 6 - 1 */
    CHECK_EQ(sh_settings_step(&s, SH_SET_DIFF_OFF, 4, 1), 5);
    CHECK_EQ(sh_settings_step(&s, SH_SET_COL_MIN, 10, -1), 10);
    CHECK_EQ(sh_settings_step(&s, SH_SET_COL_MIN, 80, 1), 80);
}

static void test_sanitize(void)
{
    sh_settings_t s = DEF;
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), 0);

    s = (sh_settings_t){ .v = { 85, 10, 20, 15, 900 } };    /* all at the limits: kept */
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), 0);
    CHECK_EQ(s.v[SH_SET_TANK_MAX], 85);

    s = (sh_settings_t){ .v = { 86, 9, 6, 4, 75 } };
    CHECK_EQ(sh_settings_sanitize(&s, &DEF),
             (1u << SH_SET_TANK_MAX) | (1u << SH_SET_COL_MIN) | (1u << SH_SET_TEST_S));
    CHECK_EQ(s.v[SH_SET_TANK_MAX], 70);
    CHECK_EQ(s.v[SH_SET_COL_MIN], 25);
    CHECK_EQ(s.v[SH_SET_TEST_S], 300);

    s = (sh_settings_t){ .v = { 60, 30, 8, 8, 300 } };      /* d.oF not below d.on: both */
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), (1u << SH_SET_DIFF_ON) | (1u << SH_SET_DIFF_OFF));
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 6);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF], 4);
    CHECK_EQ(s.v[SH_SET_TANK_MAX], 60);                      /* the others kept */

    s = (sh_settings_t){ .v = { 60, 30, 5, 9, 300 } };
    sh_settings_sanitize(&s, &DEF);
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 6);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF], 4);

    s = (sh_settings_t){ .v = { 60, 30, 9, 8, 300 } };      /* 8 < 9: kept */
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), 0);

    s = (sh_settings_t){ .v = { 60, 30, 2, 1, 300 } };      /* d.on out of range: default 6, 1 < 6 */
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), 1u << SH_SET_DIFF_ON);
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 6);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF], 1);

    s = (sh_settings_t){ .v = { 60, 30, 5, 16, 300 } };     /* d.oF out of range: default 4 < 5 */
    sh_settings_sanitize(&s, &DEF);
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 5);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF], 4);
}

int main(void)
{
    test_table();
    test_value_ok();
    test_range_coupling();
    test_step();
    test_sanitize();
    return test_summary("test_settings");
}
