/* Host test of the user settings table and checks (main/sh_settings.c;
 * doc/tasks/step2-settings-menu-pump-sensors.md 3.1). */

#include <string.h>
#include "test_util.h"
#include "sh_settings.h"

static const sh_settings_t DEF = { .v = { 70, 25, 6, 4, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };

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

    s = (sh_settings_t){ .v = { 85, 10, 20, 15, 900, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };    /* all at the limits: kept */
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), 0);
    CHECK_EQ(s.v[SH_SET_TANK_MAX], 85);

    s = (sh_settings_t){ .v = { 86, 9, 6, 4, 75, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };
    CHECK_EQ(sh_settings_sanitize(&s, &DEF),
             (1u << SH_SET_TANK_MAX) | (1u << SH_SET_COL_MIN) | (1u << SH_SET_TEST_S));
    CHECK_EQ(s.v[SH_SET_TANK_MAX], 70);
    CHECK_EQ(s.v[SH_SET_COL_MIN], 25);
    CHECK_EQ(s.v[SH_SET_TEST_S], 300);

    s = (sh_settings_t){ .v = { 60, 30, 8, 8, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };      /* d.oF not below d.on: both */
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), (1u << SH_SET_DIFF_ON) | (1u << SH_SET_DIFF_OFF));
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 6);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF], 4);
    CHECK_EQ(s.v[SH_SET_TANK_MAX], 60);                      /* the others kept */

    s = (sh_settings_t){ .v = { 60, 30, 5, 9, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };
    sh_settings_sanitize(&s, &DEF);
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 6);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF], 4);

    s = (sh_settings_t){ .v = { 60, 30, 9, 8, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };      /* 8 < 9: kept */
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), 0);

    s = (sh_settings_t){ .v = { 60, 30, 2, 1, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };      /* d.on out of range: default 6, 1 < 6 */
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), 1u << SH_SET_DIFF_ON);
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 6);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF], 1);

    s = (sh_settings_t){ .v = { 60, 30, 5, 16, 300, 15, 5, 3, SH_SEASON_AUTO, 0, 1, 0 } };     /* d.oF out of range: default 4 < 5 */
    sh_settings_sanitize(&s, &DEF);
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 5);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF], 4);
}

/* The winter set, the season, the vacation and the night cooling switches (step 10, 3.3, 3.4). */
static void test_seasons(void)
{
    CHECK_EQ(DEF.v[SH_SET_COL_MIN_W], 15);          /* winter 15 / 5 / 3, summer 25 / 6 / 4 */
    CHECK_EQ(DEF.v[SH_SET_DIFF_ON_W], 5);
    CHECK_EQ(DEF.v[SH_SET_DIFF_OFF_W], 3);
    CHECK_EQ(sh_setting_info[SH_SET_COL_MIN_W].min, 10);
    CHECK_EQ(sh_setting_info[SH_SET_COL_MIN_W].max, 80);
    CHECK_EQ(sh_setting_info[SH_SET_DIFF_ON_W].min, 3);
    CHECK_EQ(sh_setting_info[SH_SET_DIFF_ON_W].max, 20);
    CHECK_EQ(sh_setting_info[SH_SET_DIFF_OFF_W].min, 1);
    CHECK_EQ(sh_setting_info[SH_SET_DIFF_OFF_W].max, 15);
    CHECK(sh_settings_value_ok(SH_SET_SEASON, SH_SEASON_SUMMER));
    CHECK(sh_settings_value_ok(SH_SET_SEASON, SH_SEASON_AUTO));
    CHECK(!sh_settings_value_ok(SH_SET_SEASON, 3));
    CHECK(!sh_settings_value_ok(SH_SET_SEASON, -1));
    CHECK(sh_settings_value_ok(SH_SET_VACATION, 1));
    CHECK(!sh_settings_value_ok(SH_SET_VACATION, 2));
    CHECK(!sh_settings_value_ok(SH_SET_NCOOL, 2));
    CHECK(!sh_settings_value_ok(SH_SET_NCOOL_W, -1));

    /* the seasonal values and their winter twins */
    CHECK_EQ(sh_settings_seasonal(SH_SET_COL_MIN, false), SH_SET_COL_MIN);
    CHECK_EQ(sh_settings_seasonal(SH_SET_COL_MIN, true), SH_SET_COL_MIN_W);
    CHECK_EQ(sh_settings_seasonal(SH_SET_DIFF_ON, true), SH_SET_DIFF_ON_W);
    CHECK_EQ(sh_settings_seasonal(SH_SET_DIFF_OFF, true), SH_SET_DIFF_OFF_W);
    CHECK_EQ(sh_settings_seasonal(SH_SET_NCOOL, true), SH_SET_NCOOL_W);
    CHECK_EQ(sh_settings_seasonal(SH_SET_TANK_MAX, true), SH_SET_TANK_MAX);     /* common */
    CHECK_EQ(sh_settings_seasonal(SH_SET_TEST_S, true), SH_SET_TEST_S);

    /* each season couples its own pair */
    sh_settings_t s = DEF;                          /* summer 6 / 4, winter 5 / 3 */
    int16_t min, max;
    sh_settings_range(&s, SH_SET_DIFF_ON_W, &min, &max);
    CHECK_EQ(min, 4);
    CHECK_EQ(max, 20);
    sh_settings_range(&s, SH_SET_DIFF_OFF_W, &min, &max);
    CHECK_EQ(min, 1);
    CHECK_EQ(max, 4);
    sh_settings_range(&s, SH_SET_DIFF_ON, &min, &max);
    CHECK_EQ(min, 5);
    CHECK_EQ(sh_settings_step(&s, SH_SET_DIFF_OFF_W, 4, 1), 4);
    CHECK_EQ(sh_settings_step(&s, SH_SET_SEASON, SH_SEASON_AUTO, 1), SH_SEASON_AUTO);
    CHECK_EQ(sh_settings_step(&s, SH_SET_VACATION, 0, 1), 1);

    /* stored values: a broken winter pair -> its defaults, the summer pair kept */
    s = DEF;
    s.v[SH_SET_DIFF_ON] = 9;
    s.v[SH_SET_DIFF_OFF] = 8;
    s.v[SH_SET_DIFF_ON_W] = 4;
    s.v[SH_SET_DIFF_OFF_W] = 4;
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), (1u << SH_SET_DIFF_ON_W) | (1u << SH_SET_DIFF_OFF_W));
    CHECK_EQ(s.v[SH_SET_DIFF_ON_W], 5);
    CHECK_EQ(s.v[SH_SET_DIFF_OFF_W], 3);
    CHECK_EQ(s.v[SH_SET_DIFF_ON], 9);
    s = DEF;
    s.v[SH_SET_SEASON] = 7;
    s.v[SH_SET_VACATION] = 1;
    s.v[SH_SET_NCOOL] = 0;
    CHECK_EQ(sh_settings_sanitize(&s, &DEF), 1u << SH_SET_SEASON);
    CHECK_EQ(s.v[SH_SET_SEASON], SH_SEASON_AUTO);
    CHECK_EQ(s.v[SH_SET_VACATION], 1);
    CHECK_EQ(s.v[SH_SET_NCOOL], 0);
}

int main(void)
{
    test_seasons();
    test_table();
    test_value_ok();
    test_range_coupling();
    test_step();
    test_sanitize();
    return test_summary("test_settings");
}
