/* Host test of the installer data and the computed limits (main/sh_installer.c;
 * doc/tasks/step6-installer-web.md 3.1 - 3.3): ranges and checks, the boiling point at the points of
 * the glycol table and between them, the rounding down, the protection cap, the frost threshold.
 * Temperatures are tenths of °C, the pressure tenths of bar (gauge). */

#include "test_util.h"
#include "sh_installer.h"

#define W   SH_COOLANT_WATER
#define G   SH_COOLANT_GLYCOL

static const sh_installer_t DEF = { .v = { W, 0, 15, 120 } };

/* ---- ranges and checks ---------------------------------------------------------------- */

static void test_ranges(void)
{
    CHECK(sh_installer_value_ok(SH_INST_COOLANT, 0));
    CHECK(sh_installer_value_ok(SH_INST_COOLANT, 1));
    CHECK(!sh_installer_value_ok(SH_INST_COOLANT, 2));
    CHECK(!sh_installer_value_ok(SH_INST_COOLANT, -1));
    CHECK(sh_installer_value_ok(SH_INST_GLYCOL, 0));
    CHECK(sh_installer_value_ok(SH_INST_GLYCOL, 60));
    CHECK(!sh_installer_value_ok(SH_INST_GLYCOL, 61));
    CHECK(!sh_installer_value_ok(SH_INST_GLYCOL, -1));
    CHECK(!sh_installer_value_ok(SH_INST_P_MIN, 4));        /* 0.4 bar */
    CHECK(sh_installer_value_ok(SH_INST_P_MIN, 5));
    CHECK(sh_installer_value_ok(SH_INST_P_MIN, 60));
    CHECK(!sh_installer_value_ok(SH_INST_P_MIN, 61));
    CHECK(!sh_installer_value_ok(SH_INST_COL_MAX, 99));
    CHECK(sh_installer_value_ok(SH_INST_COL_MAX, 100));
    CHECK(sh_installer_value_ok(SH_INST_COL_MAX, 250));
    CHECK(!sh_installer_value_ok(SH_INST_COL_MAX, 251));
    CHECK(!sh_installer_value_ok(SH_INST_COUNT, 0));
}

static void test_sanitize(void)
{
    sh_installer_t s = { .v = { G, 10, 22, 150 } };     /* all valid: kept */
    CHECK_EQ(sh_installer_sanitize(&s, &DEF), 0);
    CHECK_EQ(s.v[SH_INST_GLYCOL], 10);
    CHECK_EQ(s.v[SH_INST_P_MIN], 22);

    s = (sh_installer_t){ .v = { 5, 70, 0, 300 } };     /* all out of range: defaults */
    CHECK_EQ(sh_installer_sanitize(&s, &DEF), 0xF);
    CHECK_EQ(s.v[SH_INST_COOLANT], W);
    CHECK_EQ(s.v[SH_INST_GLYCOL], 0);
    CHECK_EQ(s.v[SH_INST_P_MIN], 15);
    CHECK_EQ(s.v[SH_INST_COL_MAX], 120);

    s = (sh_installer_t){ .v = { W, 20, 15, 120 } };    /* glycol share with water -> 0 */
    CHECK_EQ(sh_installer_sanitize(&s, &DEF), 1u << SH_INST_GLYCOL);
    CHECK_EQ(s.v[SH_INST_GLYCOL], 0);
}

static void test_apply(void)
{
    sh_installer_t s = DEF;
    CHECK(!sh_installer_apply(&s, SH_INST_GLYCOL, 10));    /* water: only 0 */
    CHECK_EQ(s.v[SH_INST_GLYCOL], 0);
    CHECK(sh_installer_apply(&s, SH_INST_GLYCOL, 0));
    CHECK(sh_installer_apply(&s, SH_INST_COOLANT, G));
    CHECK(sh_installer_apply(&s, SH_INST_GLYCOL, 30));
    CHECK_EQ(s.v[SH_INST_GLYCOL], 30);
    CHECK(!sh_installer_apply(&s, SH_INST_GLYCOL, 61));    /* out of range: unchanged */
    CHECK_EQ(s.v[SH_INST_GLYCOL], 30);
    CHECK(sh_installer_apply(&s, SH_INST_COOLANT, W));     /* water clears the share */
    CHECK_EQ(s.v[SH_INST_COOLANT], W);
    CHECK_EQ(s.v[SH_INST_GLYCOL], 0);
    CHECK(!sh_installer_apply(&s, SH_INST_P_MIN, 61));
    CHECK(sh_installer_apply(&s, SH_INST_P_MIN, 60));
    CHECK_EQ(s.v[SH_INST_P_MIN], 60);
    CHECK(!sh_installer_apply(&s, SH_INST_COL_MAX, 99));
    CHECK(sh_installer_apply(&s, SH_INST_COL_MAX, 250));
    CHECK_EQ(s.v[SH_INST_COL_MAX], 250);
}

/* ---- boiling point ------------------------------------------------------------------- */

static void test_boiling_water(void)
{
    /* the checks of the task: water, 1.5 bar -> ~127 °C (127.76, rounded down) */
    CHECK_EQ(sh_boiling_point(W, 0, 15), 1277);
    CHECK_EQ(sh_boiling_point(W, 0, 5), 1116);              /* 0.5 bar: 111.63 */
    CHECK_EQ(sh_boiling_point(W, 0, 22), 1361);             /* 2.2 bar: 136.11 */
    CHECK_EQ(sh_boiling_point(W, 0, 60), 1653);             /* 6.0 bar: 165.31 */
    /* water ignores a glycol share (there is none) */
    CHECK_EQ(sh_boiling_point(W, 40, 15), 1277);
}

static void test_boiling_glycol_points(void)
{
    /* 1.5 bar (water 127.76 °C) + 0 / 1 / 2 / 3 / 4 / 6 K at 0 / 20 / 30 / 40 / 50 / 60 % */
    CHECK_EQ(sh_boiling_point(G, 0, 15), 1277);
    CHECK_EQ(sh_boiling_point(G, 20, 15), 1287);
    CHECK_EQ(sh_boiling_point(G, 30, 15), 1297);
    CHECK_EQ(sh_boiling_point(G, 40, 15), 1307);
    CHECK_EQ(sh_boiling_point(G, 50, 15), 1317);
    CHECK_EQ(sh_boiling_point(G, 60, 15), 1337);
}

static void test_boiling_glycol_between(void)
{
    /* 2.2 bar (water 136.11 °C) */
    CHECK_EQ(sh_boiling_point(G, 9, 22), 1365);             /* ~9 % PG -> ~136 °C: +0.45 K, 136.56 */
    CHECK_EQ(sh_boiling_point(G, 10, 22), 1366);            /* +0.5 K */
    CHECK_EQ(sh_boiling_point(G, 25, 22), 1376);            /* +1.5 K */
    CHECK_EQ(sh_boiling_point(G, 55, 22), 1411);            /* +5 K */
    CHECK_EQ(sh_boiling_point(G, 1, 22), 1361);             /* +0.05 K: 136.16, rounded down */
}

/* ---- limits ------------------------------------------------------------------------- */

static void test_protect_cap(void)
{
    CHECK_EQ(sh_protect_cap(120, 1277), 1150);              /* defaults: the collector max - 5 K */
    CHECK_EQ(sh_protect_cap(150, 1365), 1265);              /* ~9 % PG, 2.2 bar: boiling - 10 K */
    CHECK_EQ(sh_protect_cap(122, 1270), 1170);              /* equal */
    CHECK_EQ(sh_protect_cap(100, 1116), 950);               /* 0.5 bar, 100 °C collector */
}

static void test_frost_threshold(void)
{
    CHECK_EQ(sh_frost_threshold(W, 0), 40);                 /* water: 4 °C */
    CHECK_EQ(sh_frost_threshold(W, 30), 40);
    /* glycol: freezing point + 4 K, at the points of the table */
    CHECK_EQ(sh_frost_threshold(G, 0), 40);
    CHECK_EQ(sh_frost_threshold(G, 10), 10);                /* -3 + 4 */
    CHECK_EQ(sh_frost_threshold(G, 20), -40);               /* -8 + 4 */
    CHECK_EQ(sh_frost_threshold(G, 30), -100);
    CHECK_EQ(sh_frost_threshold(G, 40), -180);
    CHECK_EQ(sh_frost_threshold(G, 50), -290);
    CHECK_EQ(sh_frost_threshold(G, 60), -400);              /* -48 + 4 = -44 -> held at -40 */
    /* between the points */
    CHECK_EQ(sh_frost_threshold(G, 9), 13);                 /* ~9 % PG -> ~1 - 2 °C */
    CHECK_EQ(sh_frost_threshold(G, 15), -15);               /* -5.5 + 4 */
    CHECK_EQ(sh_frost_threshold(G, 55), -365);              /* -40.5 + 4 */
    CHECK_EQ(sh_frost_threshold(G, 57), -395);              /* -43.5 + 4: just above -40 */
    CHECK_EQ(sh_frost_threshold(G, 58), -400);              /* -45 + 4 -> held */
}

static void test_limits(void)
{
    sh_limits_t l;
    sh_installer_limits(&DEF, &l);
    CHECK_EQ(l.boiling, 1277);
    CHECK_EQ(l.protect_cap, 1150);
    CHECK_EQ(l.frost, 40);

    const sh_installer_t pg = { .v = { G, 9, 22, 150 } };
    sh_installer_limits(&pg, &l);
    CHECK_EQ(l.boiling, 1365);
    CHECK_EQ(l.protect_cap, 1265);
    CHECK_EQ(l.frost, 13);
}

int main(void)
{
    test_ranges();
    test_sanitize();
    test_apply();
    test_boiling_water();
    test_boiling_glycol_points();
    test_boiling_glycol_between();
    test_protect_cap();
    test_frost_threshold();
    test_limits();
    return test_summary("test_installer");
}
