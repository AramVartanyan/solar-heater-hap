/* Host test of the installer data and the computed limits (main/sh_installer.c;
 * doc/operating-modes.md 1.5, 3.3, 3.4): ranges and checks, the boiling point at the points
 * of the glycol table and between them, the ceiling of the collector protection, the frost
 * threshold. Temperatures are tenths of °C, the pressure tenths of bar. */

#include "test_util.h"
#include "sh_installer.h"

static const sh_installer_t DEF = { .v = {
    [SH_INST_FLUID] = SH_FLUID_WATER, [SH_INST_GLYCOL] = 0, [SH_INST_PRESSURE] = 15,
    [SH_INST_COL_MAX] = 120,
    [SH_INST_COL_TYPE] = SH_COL_FLAT, [SH_INST_COL_AREA] = 20, [SH_INST_TANK_VOL] = 150,
} };

static sh_limits_t calc(int fluid, int glycol, int press, int col_max)
{
    const sh_installer_t d = { .v = {
        [SH_INST_FLUID] = (int16_t)fluid, [SH_INST_GLYCOL] = (int16_t)glycol,
        [SH_INST_PRESSURE] = (int16_t)press, [SH_INST_COL_MAX] = (int16_t)col_max,
        [SH_INST_COL_TYPE] = SH_COL_FLAT, [SH_INST_COL_AREA] = 20, [SH_INST_TANK_VOL] = 150,
    } };
    sh_limits_t lim;
    sh_limits_calc(&d, &lim);
    return lim;
}

#define WATER   SH_FLUID_WATER
#define PG      SH_FLUID_GLYCOL

static void test_ranges(void)
{
    CHECK(sh_installer_ok(&DEF));

    /* each value at the ends of its range and just outside */
    static const struct { sh_inst_t id; int16_t min, max; } R[] = {
        { SH_INST_GLYCOL, 0, 60 }, { SH_INST_PRESSURE, 5, 60 }, { SH_INST_COL_MAX, 100, 250 },
    };
    for (unsigned i = 0; i < sizeof(R) / sizeof(R[0]); i++) {
        sh_installer_t d = DEF;
        d.v[SH_INST_FLUID] = PG;
        d.v[R[i].id] = R[i].min;
        CHECK(sh_installer_ok(&d));
        d.v[R[i].id] = R[i].max;
        CHECK(sh_installer_ok(&d));
        d.v[R[i].id] = (int16_t)(R[i].min - 1);
        CHECK(!sh_installer_ok(&d));
        d.v[R[i].id] = (int16_t)(R[i].max + 1);
        CHECK(!sh_installer_ok(&d));
        CHECK_EQ(sh_inst_info[R[i].id].min, R[i].min);
        CHECK_EQ(sh_inst_info[R[i].id].max, R[i].max);
    }
    sh_installer_t d = DEF;
    d.v[SH_INST_FLUID] = 2;                     /* neither water nor glycol */
    CHECK(!sh_installer_ok(&d));
    d.v[SH_INST_FLUID] = -1;
    CHECK(!sh_installer_ok(&d));

    /* water has no glycol */
    d = DEF;
    d.v[SH_INST_GLYCOL] = 10;
    CHECK(!sh_installer_ok(&d));
    d.v[SH_INST_FLUID] = PG;
    CHECK(sh_installer_ok(&d));
}

static void test_sanitize(void)
{
    sh_installer_t d = DEF;
    CHECK_EQ(sh_installer_sanitize(&d, &DEF), 0);

    /* each value out of range -> its default, the others stay */
    d = (sh_installer_t){ .v = { PG, 61, 22, 150, SH_COL_FLAT, 20, 150 } };
    CHECK_EQ(sh_installer_sanitize(&d, &DEF), 1u << SH_INST_GLYCOL);
    CHECK_EQ(d.v[SH_INST_GLYCOL], 0);
    CHECK_EQ(d.v[SH_INST_FLUID], PG);
    CHECK_EQ(d.v[SH_INST_PRESSURE], 22);
    CHECK_EQ(d.v[SH_INST_COL_MAX], 150);

    d = (sh_installer_t){ .v = { PG, 30, 4, 251, SH_COL_FLAT, 20, 150 } };
    CHECK_EQ(sh_installer_sanitize(&d, &DEF), (1u << SH_INST_PRESSURE) | (1u << SH_INST_COL_MAX));
    CHECK_EQ(d.v[SH_INST_PRESSURE], 15);
    CHECK_EQ(d.v[SH_INST_COL_MAX], 120);
    CHECK_EQ(d.v[SH_INST_GLYCOL], 30);

    d = (sh_installer_t){ .v = { 7, 30, 22, 150, SH_COL_FLAT, 20, 150 } };    /* unknown fluid -> water, so no glycol */
    CHECK_EQ(sh_installer_sanitize(&d, &DEF), (1u << SH_INST_FLUID) | (1u << SH_INST_GLYCOL));
    CHECK_EQ(d.v[SH_INST_FLUID], WATER);
    CHECK_EQ(d.v[SH_INST_GLYCOL], 0);

    d = (sh_installer_t){ .v = { WATER, 30, 22, 150, SH_COL_FLAT, 20, 150 } };    /* glycol stored with water -> 0 */
    CHECK_EQ(sh_installer_sanitize(&d, &DEF), 1u << SH_INST_GLYCOL);
    CHECK_EQ(d.v[SH_INST_GLYCOL], 0);
    CHECK(sh_installer_ok(&d));
}

static void test_boiling_water(void)
{
    /* Antoine at gauge + 1.013 bar, rounded down to 0.1 °C */
    CHECK_EQ(calc(WATER, 0, 15, 120).boiling, 1277);    /* 127.76: the defaults, "~127 °C" */
    CHECK_EQ(calc(WATER, 0, 5, 120).boiling, 1116);     /* 111.63 */
    CHECK_EQ(calc(WATER, 0, 10, 120).boiling, 1205);    /* 120.53 */
    CHECK_EQ(calc(WATER, 0, 22, 120).boiling, 1361);    /* 136.11 */
    CHECK_EQ(calc(WATER, 0, 30, 120).boiling, 1439);    /* 143.99: down, not to the nearest */
    CHECK_EQ(calc(WATER, 0, 60, 120).boiling, 1653);    /* 165.31 */

    /* higher pressure -> higher boiling point, over the whole range */
    for (int p = 6; p <= 60; p++) {
        CHECK(calc(WATER, 0, p, 120).boiling > calc(WATER, 0, p - 1, 120).boiling);
    }
    /* the glycol value is not used with water */
    CHECK_EQ(calc(WATER, 40, 15, 120).boiling, 1277);
}

static void test_boiling_glycol(void)
{
    /* the points of the table at 1.5 bar (water 127.76 °C): + 0 / 1 / 2 / 3 / 4 / 6 K */
    CHECK_EQ(calc(PG, 0, 15, 120).boiling, 1277);
    CHECK_EQ(calc(PG, 20, 15, 120).boiling, 1287);
    CHECK_EQ(calc(PG, 30, 15, 120).boiling, 1297);
    CHECK_EQ(calc(PG, 40, 15, 120).boiling, 1307);
    CHECK_EQ(calc(PG, 50, 15, 120).boiling, 1317);
    CHECK_EQ(calc(PG, 60, 15, 120).boiling, 1337);

    /* between the points: linear */
    CHECK_EQ(calc(PG, 10, 15, 120).boiling, 1282);      /* + 0.5 K -> 128.26 */
    CHECK_EQ(calc(PG, 1, 15, 120).boiling, 1278);       /* + 0.05 K -> 127.81 */
    CHECK_EQ(calc(PG, 25, 15, 120).boiling, 1292);      /* + 1.5 K */
    CHECK_EQ(calc(PG, 35, 15, 120).boiling, 1302);      /* + 2.5 K */
    CHECK_EQ(calc(PG, 45, 15, 120).boiling, 1312);      /* + 3.5 K */
    CHECK_EQ(calc(PG, 55, 15, 120).boiling, 1327);      /* + 5 K (the last step is 2 K) */
    CHECK_EQ(calc(PG, 59, 15, 120).boiling, 1335);      /* + 5.8 K -> 133.56 */

    /* the example of doc/operating-modes.md: ~9 % (1:10), 2.2 bar -> ~136 °C */
    CHECK_EQ(calc(PG, 9, 22, 120).boiling, 1365);       /* 136.11 + 0.45 = 136.56 */
}

static void test_protect_cap(void)
{
    /* min(collector maximum - 5 K, boiling point - 10 K) */
    CHECK_EQ(calc(WATER, 0, 15, 120).protect_cap, 1150);    /* the defaults: the collector decides */
    CHECK_EQ(calc(WATER, 0, 15, 122).protect_cap, 1170);
    CHECK_EQ(calc(WATER, 0, 15, 123).protect_cap, 1177);    /* 118.0 > 117.7: the boiling point decides */
    CHECK_EQ(calc(WATER, 0, 15, 250).protect_cap, 1177);
    CHECK_EQ(calc(WATER, 0, 5, 120).protect_cap, 1016);
    CHECK_EQ(calc(WATER, 0, 5, 100).protect_cap, 950);
    CHECK_EQ(calc(PG, 9, 22, 150).protect_cap, 1265);       /* doc: "126 °C" */
    CHECK_EQ(calc(PG, 9, 22, 120).protect_cap, 1150);
    CHECK_EQ(calc(PG, 60, 60, 250).protect_cap, 1613);      /* the highest possible: 171.3 - 10 */
}

static void test_frost(void)
{
    CHECK_EQ(calc(WATER, 0, 15, 120).frost, 40);        /* water: 4 °C */
    CHECK_EQ(calc(WATER, 30, 15, 120).frost, 40);       /* the glycol value is not used with water */

    /* the points of the table + 4 K: 0 / -3 / -8 / -14 / -22 / -33 / -48 °C */
    CHECK_EQ(calc(PG, 0, 15, 120).frost, 40);
    CHECK_EQ(calc(PG, 10, 15, 120).frost, 10);
    CHECK_EQ(calc(PG, 20, 15, 120).frost, -40);
    CHECK_EQ(calc(PG, 30, 15, 120).frost, -100);
    CHECK_EQ(calc(PG, 40, 15, 120).frost, -180);
    CHECK_EQ(calc(PG, 50, 15, 120).frost, -290);
    CHECK_EQ(calc(PG, 60, 15, 120).frost, -400);        /* -44 °C: held at -40 °C */

    /* between the points: linear */
    CHECK_EQ(calc(PG, 9, 15, 120).frost, 13);           /* doc: "~1 - 2 °C" */
    CHECK_EQ(calc(PG, 5, 15, 120).frost, 25);
    CHECK_EQ(calc(PG, 15, 15, 120).frost, -15);
    CHECK_EQ(calc(PG, 25, 15, 120).frost, -70);
    CHECK_EQ(calc(PG, 35, 15, 120).frost, -140);
    CHECK_EQ(calc(PG, 45, 15, 120).frost, -235);
    CHECK_EQ(calc(PG, 55, 15, 120).frost, -365);
    CHECK_EQ(calc(PG, 57, 15, 120).frost, -395);        /* -39.5 °C: just inside the range */
    CHECK_EQ(calc(PG, 58, 15, 120).frost, -400);        /* -41 °C: held */

    /* always within -40 ... +10 °C, never rising with the concentration */
    for (int pct = 1; pct <= 60; pct++) {
        const int16_t f = calc(PG, pct, 15, 120).frost;
        CHECK(f >= -400 && f <= 100);
        CHECK(f <= calc(PG, pct - 1, 15, 120).frost);
    }
    /* the pressure and the collector maximum do not matter */
    CHECK_EQ(calc(PG, 30, 60, 250).frost, -100);
}

int main(void)
{
    test_ranges();
    test_sanitize();
    test_boiling_water();
    test_boiling_glycol();
    test_protect_cap();
    test_frost();
    return test_summary("test_installer");
}
