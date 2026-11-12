/* Host test of group C (doc/tasks/step10-cooling-vacation-seasons.md 3.1 - 3.5): night cooling —
 * triggers and their hours, target, self-learning, heater (sh_cool) and the pump rule
 * (sh_pump_logic); sunrise and sunset (sh_sun); the vacation by day and at night; the seasons
 * (sh_season); the air venting cycles (sh_pump_ctl); the new installer data (sh_installer); the
 * indication (sh_ind) and the day (sh_day, sh_hist).
 * Temperatures are tenths of °C, times seconds; local time in minutes of the day. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_cool.h"
#include "sh_sun.h"
#include "sh_season.h"
#include "sh_pump_ctl.h"
#include "sh_installer.h"
#include "sh_alarm.h"
#include "sh_ind.h"
#include "sh_day.h"
#include "sh_hist.h"
#include "sh_clock_rules.h"

#define HM(h, m)    ((uint16_t)((h) * 60 + (m)))

/* The defaults: differences 6 / 4 K, minimum times 3 / 2 min, Tmax 70 °C, C.Lo 25 °C, collector
 * protection 110 °C (ceiling 115 °C), boiling point 127.7 °C, frost threshold 4 °C. */
static const sh_pump_params_t P = {
    .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120, .tank_max = 700, .col_min = 250,
    .col_protect = 1100, .col_protect_cap = 1150, .col_boiling = 1277, .frost_on = true, .frost = 40,
};

/* ---- installer data (3.1) ------------------------------------------------------------------ */

static sh_installer_t inst(int fluid, int glycol, int col_max)
{
    const sh_installer_t d = { .v = {
        [SH_INST_FLUID] = (int16_t)fluid, [SH_INST_GLYCOL] = (int16_t)glycol, [SH_INST_PRESSURE] = 15,
        [SH_INST_COL_MAX] = (int16_t)col_max, [SH_INST_COL_TYPE] = SH_COL_FLAT, [SH_INST_COL_AREA] = 20,
        [SH_INST_TANK_VOL] = 150,
    } };
    return d;
}

static void test_installer_fields(void)
{
    sh_installer_t d = inst(SH_FLUID_WATER, 0, 120);
    CHECK(sh_installer_ok(&d));

    /* kind: flat / tubes; area 0.5 - 20.0 m², step 0.1; volume 50 - 1000 l, step 10 */
    static const struct { sh_inst_t id; int16_t min, max, below, above; } R[] = {
        { SH_INST_COL_TYPE, SH_COL_FLAT, SH_COL_TUBES, -1, 2 },
        { SH_INST_COL_AREA, 5, 200, 4, 201 },
        { SH_INST_TANK_VOL, 50, 1000, 40, 1010 },
    };
    for (unsigned i = 0; i < sizeof(R) / sizeof(R[0]); i++) {
        d = inst(SH_FLUID_WATER, 0, 120);
        d.v[R[i].id] = R[i].min;
        CHECK(sh_installer_ok(&d));
        d.v[R[i].id] = R[i].max;
        CHECK(sh_installer_ok(&d));
        d.v[R[i].id] = R[i].below;
        CHECK(!sh_installer_ok(&d));
        d.v[R[i].id] = R[i].above;
        CHECK(!sh_installer_ok(&d));
    }
    d = inst(SH_FLUID_WATER, 0, 120);
    d.v[SH_INST_TANK_VOL] = 155;                    /* off the 10 l step */
    CHECK(!sh_installer_ok(&d));
    d.v[SH_INST_COL_AREA] = 18;                     /* 1.8 m² */
    d.v[SH_INST_TANK_VOL] = 120;
    CHECK(sh_installer_ok(&d));

    /* stored values out of range -> the defaults (flat, 2.0 m², 150 l) */
    const sh_installer_t def = inst(SH_FLUID_WATER, 0, 120);
    d = def;
    d.v[SH_INST_COL_TYPE] = 5;
    d.v[SH_INST_COL_AREA] = 0;
    d.v[SH_INST_TANK_VOL] = 1234;
    CHECK_EQ(sh_installer_sanitize(&d, &def),
             (1u << SH_INST_COL_TYPE) | (1u << SH_INST_COL_AREA) | (1u << SH_INST_TANK_VOL));
    CHECK_EQ(d.v[SH_INST_COL_TYPE], SH_COL_FLAT);
    CHECK_EQ(d.v[SH_INST_COL_AREA], 20);
    CHECK_EQ(d.v[SH_INST_TANK_VOL], 150);
}

/* Vacation limit: 75 % of the collector maximum, at most the boiling point - 10 K
 * (doc: water, 1.5 bar -> 120 °C: 90; 150 °C: 112; 180 °C: 117). */
static void test_vacation_limit(void)
{
    sh_limits_t lim;
    sh_installer_t d = inst(SH_FLUID_WATER, 0, 120);
    sh_limits_calc(&d, &lim);
    CHECK_EQ(lim.boiling, 1277);
    CHECK_EQ(lim.vacation, 900);
    d = inst(SH_FLUID_WATER, 0, 150);
    sh_limits_calc(&d, &lim);
    CHECK_EQ(lim.vacation, 1125);
    d = inst(SH_FLUID_WATER, 0, 180);
    sh_limits_calc(&d, &lim);
    CHECK_EQ(lim.vacation, 1177);                   /* 127.7 - 10 */
    d = inst(SH_FLUID_WATER, 0, 156);               /* 117.0 < 117.7 */
    sh_limits_calc(&d, &lim);
    CHECK_EQ(lim.vacation, 1170);
    d = inst(SH_FLUID_WATER, 0, 157);               /* 117.75 -> the boiling point decides */
    sh_limits_calc(&d, &lim);
    CHECK_EQ(lim.vacation, 1177);
    for (int max = 100; max <= 250; max++) {        /* never above the ceiling of the normal protection */
        d = inst(SH_FLUID_GLYCOL, 40, max);
        sh_limits_calc(&d, &lim);
        CHECK(lim.vacation <= lim.protect_cap);
    }
}

/* Air venting pause: 10 min x the viscosity coefficient (water 1; PG 10 % 1.3, 30 % 2.5, 40 % 3.5,
 * 50 % 4.5, 60 % 5.5, linear in between). */
static void test_air_pause(void)
{
    sh_installer_t d = inst(SH_FLUID_WATER, 0, 120);
    CHECK_EQ(sh_air_pause_s(&d), 600);
    CHECK_EQ(sh_air_total_s(sh_air_pause_s(&d)), 45 * 60);      /* water: 3 x (5 + 10) = 45 min */
    static const struct { int pct; uint32_t pause_s; } T[] = {
        { 0, 600 }, { 10, 780 }, { 20, 1140 }, { 30, 1500 }, { 35, 1800 }, { 40, 2100 }, { 50, 2700 },
        { 60, 3300 }, { 5, 690 }, { 9, 762 },
    };
    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        d = inst(SH_FLUID_GLYCOL, T[i].pct, 120);
        CHECK_EQ(sh_air_pause_s(&d), T[i].pause_s);
    }
    CHECK_EQ(sh_air_total_s(3300), 180 * 60);                   /* the longest: 3 x (5 + 55) min */
}

/* ---- target of the night cooling (3.2) ----------------------------------------------------- */

static void test_cool_target(void)
{
    /* doc: 1.8 m² flat, 120 l: 4.5 kWh -> ~32 K -> ~63 °C */
    CHECK_EQ(sh_cool_target(false, 18, 120, 700, 0), 628);
    /* the defaults: 2.0 m² flat, 150 l: 5 kWh / (150 x 1.163) = 28.7 K -> 66.3 °C */
    CHECK_EQ(sh_cool_target(false, 20, 150, 700, 0), 663);
    /* tubes: 3.0 kWh/m²: 5.4 kWh -> 38.7 K -> 56.3 °C */
    CHECK_EQ(sh_cool_target(true, 18, 120, 700, 0), 563);

    /* held within Tmax - 20 K ... Tmax */
    CHECK_EQ(sh_cool_target(false, 200, 50, 700, 0), 500);      /* a huge collector on a small tank */
    CHECK_EQ(sh_cool_target(false, 5, 1000, 700, 0), 700);      /* 1 K a day: nothing to cool below Tmax */
    CHECK_EQ(sh_cool_target(false, 18, 120, 850, 0), 650);      /* Tmax 85: not below 65 */
    CHECK_EQ(sh_cool_target(false, 18, 120, 600, 0), 600);      /* Tmax 60: not above it */
    CHECK_EQ(sh_cool_target(false, 18, 120, 650, 0), 628);

    /* the learned correction, held within the same range */
    CHECK_EQ(sh_cool_target(false, 18, 120, 700, -3), 598);
    CHECK_EQ(sh_cool_target(false, 18, 120, 700, -12), 508);
    CHECK_EQ(sh_cool_target(false, 18, 120, 700, -13), 500);
    CHECK_EQ(sh_cool_target(false, 18, 120, 700, -20), 500);
    CHECK_EQ(sh_cool_target(false, 5, 1000, 700, -3), 670);     /* it acts on the limited target */
}

/* ---- sessions: triggers and hours (3.2) ---------------------------------------------------- */

static sh_cool_in_t cin(uint16_t min, int16_t tank)
{
    const sh_cool_in_t in = {
        .reliable = true, .min = min, .t_col = 150, .t_tank = tank, .tank_max = 700, .target = 663,
        .tmax_on = true,
    };
    return in;
}

static void step_at(sh_cool_t *c, uint16_t min, int16_t tank)
{
    const sh_cool_in_t in = cin(min, tank);
    sh_cool_step(c, &in, 0);
}

/* a) tank above Tmax: the window 04:00 - 07:00 */
static void test_trigger_tmax(void)
{
    sh_cool_t c = { 0 };
    step_at(&c, HM(1, 0), 720);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    step_at(&c, HM(3, 0), 720);                     /* 03:00 is for b) only */
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    step_at(&c, HM(3, 59), 720);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    step_at(&c, HM(4, 0), 720);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);
    CHECK_EQ(c.events, SH_COOL_EVT_START);
    CHECK_EQ(c.target, 663);
    step_at(&c, HM(4, 1), 720);
    CHECK_EQ(c.events, 0);
    step_at(&c, HM(6, 59), 700);                    /* still above the target */
    CHECK_EQ(c.trigger, SH_COOL_TMAX);
    step_at(&c, HM(7, 0), 700);                     /* the hard limit */
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    CHECK_EQ(c.events & SH_COOL_EVT_END, SH_COOL_EVT_END);
    CHECK_EQ(c.end, SH_COOL_END_TIME);

    /* it may start later in the window: when the tank reaches Tmax */
    step_at(&c, HM(12, 0), 600);
    step_at(&c, HM(4, 30), 699);
    CHECK_EQ(c.trigger, SH_COOL_NONE);              /* above the target, but not at Tmax */
    step_at(&c, HM(5, 0), 700);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);
    step_at(&c, HM(5, 30), 664);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);
    step_at(&c, HM(5, 31), 663);                    /* the target */
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    CHECK_EQ(c.end, SH_COOL_END_TARGET);
    step_at(&c, HM(5, 40), 720);                    /* one session a night */
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    step_at(&c, HM(7, 0), 720);
    step_at(&c, HM(4, 0), 720);                     /* the next night */
    CHECK_EQ(c.trigger, SH_COOL_TMAX);

    /* the seasonal switch (winter: off) */
    sh_cool_t w = { 0 };
    sh_cool_in_t in = cin(HM(4, 30), 720);
    in.tmax_on = false;
    sh_cool_step(&w, &in, 0);
    CHECK_EQ(w.trigger, SH_COOL_NONE);

    /* Tmax is the target (nothing to gain): no session */
    sh_cool_t t = { 0 };
    in = cin(HM(4, 30), 700);
    in.target = 700;
    sh_cool_step(&t, &in, 0);
    CHECK_EQ(t.trigger, SH_COOL_NONE);
    in.t_tank = 701;
    sh_cool_step(&t, &in, 0);
    CHECK_EQ(t.trigger, SH_COOL_TMAX);

    /* not at night before the window, not in the evening */
    sh_cool_t e = { 0 };
    step_at(&e, HM(22, 0), 800);
    CHECK_EQ(e.trigger, SH_COOL_NONE);
    step_at(&e, HM(0, 0), 800);
    CHECK_EQ(e.trigger, SH_COOL_NONE);
}

/* b) E4 during the day: from 03:00, always (also in winter) */
static void test_trigger_e4(void)
{
    sh_cool_t c = { 0 };
    sh_cool_in_t in = cin(HM(14, 0), 950);
    in.e4 = true;
    in.tmax_on = false;                             /* winter: a) is off, b) works */
    sh_cool_step(&c, &in, 0);
    CHECK(c.e4_pending);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    in = cin(HM(2, 59), 800);
    in.tmax_on = false;
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    in.min = HM(3, 0);
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_E4);
    CHECK_EQ(c.events, SH_COOL_EVT_START);
    in.min = HM(6, 59);
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_E4);
    in.min = HM(7, 0);
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    CHECK_EQ(c.end, SH_COOL_END_TIME);
    CHECK(!c.e4_pending);                           /* used: the next night has no b) */
    in.min = HM(3, 30);
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_NONE);

    /* the tank already at the target in the evening (hot water used): nothing to do */
    sh_cool_t d = { 0 };
    in = cin(HM(15, 0), 950);
    in.e4 = true;
    sh_cool_step(&d, &in, 0);
    step_at(&d, HM(3, 0), 663);
    CHECK_EQ(d.trigger, SH_COOL_NONE);
    step_at(&d, HM(3, 10), 600);
    CHECK_EQ(d.trigger, SH_COOL_NONE);

    /* b) reaches its target */
    sh_cool_t t = { 0 };
    in = cin(HM(15, 0), 950);
    in.e4 = true;
    sh_cool_step(&t, &in, 0);
    step_at(&t, HM(3, 0), 690);                     /* below Tmax: b) cools anyway */
    CHECK_EQ(t.trigger, SH_COOL_E4);
    step_at(&t, HM(3, 40), 662);
    CHECK_EQ(t.trigger, SH_COOL_NONE);
    CHECK_EQ(t.end, SH_COOL_END_TARGET);
}

/* Without a reliable clock: 6 h after the sunset detected in the same night; no sunset — no cooling. */
static void test_no_clock(void)
{
    sh_cool_t c = { 0 };
    sh_cool_in_t in = cin(0, 720);
    in.reliable = false;
    in.dark = true;
    in.sunset_s = 1000;
    sh_cool_step(&c, &in, 1000 + 6 * 3600 - 1);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    sh_cool_step(&c, &in, 1000 + 6 * 3600);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);
    sh_cool_step(&c, &in, 1000 + 10 * 3600 - 1);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);
    sh_cool_step(&c, &in, 1000 + 10 * 3600);        /* as long as 03:00 - 07:00 */
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    CHECK_EQ(c.end, SH_COOL_END_TIME);

    /* the sunrise (or the collector above the tank again) ends the night */
    sh_cool_t r = { 0 };
    sh_cool_step(&r, &in, 1000 + 7 * 3600);
    CHECK_EQ(r.trigger, SH_COOL_TMAX);
    in.dark = false;
    sh_cool_step(&r, &in, 1000 + 7 * 3600 + 60);
    CHECK_EQ(r.trigger, SH_COOL_NONE);
    CHECK_EQ(r.end, SH_COOL_END_TIME);

    /* no sunset detected: not this night, whatever the tank; an E4 waits for the next night */
    sh_cool_t n = { 0 };
    in = cin(0, 900);
    in.reliable = false;
    in.e4 = true;
    for (uint32_t t = 0; t < 24 * 3600; t += 600) {
        sh_cool_step(&n, &in, t);
        CHECK_EQ(n.trigger, SH_COOL_NONE);
    }
    in.e4 = false;
    in.dark = true;
    in.sunset_s = 90000;
    sh_cool_step(&n, &in, 90000 + 6 * 3600);
    CHECK_EQ(n.trigger, SH_COOL_E4);

    /* b) without a clock: the same window */
    sh_cool_t b = { 0 };
    in = cin(0, 690);
    in.reliable = false;
    in.e4 = true;
    sh_cool_step(&b, &in, 10);
    in.e4 = false;
    in.dark = true;
    in.sunset_s = 500;
    sh_cool_step(&b, &in, 500 + 6 * 3600 - 1);
    CHECK_EQ(b.trigger, SH_COOL_NONE);
    sh_cool_step(&b, &in, 500 + 6 * 3600);
    CHECK_EQ(b.trigger, SH_COOL_E4);
}

/* The heater works against the cooling: stop until the next night, shown as information. */
static void test_heater(void)
{
    sh_cool_t c = { 0 };
    step_at(&c, HM(4, 0), 720);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);
    sh_cool_in_t in = cin(HM(4, 5), 720);
    in.pump_cool = true;
    sh_cool_step(&c, &in, 0);
    in.t_tank = 712;                                /* cooling */
    sh_cool_step(&c, &in, 0);
    in.t_tank = 716;                                /* +0.4 K above the lowest: not yet */
    sh_cool_step(&c, &in, 0);
    CHECK(!c.heater);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);
    in.t_tank = 717;                                /* +0.5 K although the pump cools */
    sh_cool_step(&c, &in, 0);
    CHECK(c.heater);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    CHECK_EQ(c.end, SH_COOL_END_HEATER);
    in.pump_cool = false;
    in.t_tank = 760;
    in.min = HM(5, 0);
    sh_cool_step(&c, &in, 0);                       /* not again this night */
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    in.min = HM(12, 0);
    sh_cool_step(&c, &in, 0);
    CHECK(c.heater);                                /* the information stays during the day */
    in.min = HM(3, 0);
    sh_cool_step(&c, &in, 0);                       /* the next night */
    CHECK(!c.heater);
    in.min = HM(4, 0);
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);

    /* a rise while the pump does not cool is not judged here */
    sh_cool_t n = { 0 };
    step_at(&n, HM(4, 0), 720);
    step_at(&n, HM(4, 10), 740);
    CHECK(!n.heater);
    CHECK_EQ(n.trigger, SH_COOL_TMAX);

    /* a faulty sensor ends the session */
    in = cin(HM(4, 20), SH_TEMP_INVALID);
    sh_cool_step(&n, &in, 0);
    CHECK_EQ(n.trigger, SH_COOL_NONE);
    CHECK_EQ(n.end, SH_COOL_END_SENSOR);
}

/* One day for the self-learning: the night (with or without a session), then the day with or
 * without an E4. */
static void night_and_day(sh_cool_t *c, int16_t night_tank, bool e4)
{
    step_at(c, HM(3, 0), night_tank);
    step_at(c, HM(4, 0), night_tank);
    step_at(c, HM(7, 0), night_tank);
    sh_cool_in_t in = cin(HM(14, 0), 600);
    in.e4 = e4;
    sh_cool_step(c, &in, 0);
    in.e4 = false;
    in.min = HM(18, 0);
    sh_cool_step(c, &in, 0);
}

/* Self-learning: E4 on the day after a cooled night -> -3 K; 7 mornings without E4 -> +1 K, back to
 * the computed target at most. */
static void test_learning(void)
{
    sh_cool_t c = { 0 };
    night_and_day(&c, 600, true);                   /* no cooling before this E4: the target was not tried */
    CHECK_EQ(c.offset, 0);
    night_and_day(&c, 720, true);                   /* cooled after the E4 (b), E4 again */
    CHECK_EQ(c.offset, -3);
    night_and_day(&c, 720, true);
    CHECK_EQ(c.offset, -6);
    night_and_day(&c, 600, true);                   /* the tank was below the target: no session, no change */
    CHECK_EQ(c.offset, -6);

    /* an E4 that lasts or comes back the same day counts once */
    sh_cool_t d = { 0 };
    night_and_day(&d, 720, true);
    CHECK_EQ(d.offset, -3);
    sh_cool_in_t in = cin(HM(19, 0), 950);
    in.e4 = true;
    sh_cool_step(&d, &in, 0);
    CHECK_EQ(d.offset, -3);
    CHECK_EQ(d.events & SH_COOL_EVT_LEARNED, 0);

    /* 7 quiet mornings in a row: +1 K */
    c.e4_pending = false;
    for (int i = 0; i < 7; i++) {
        CHECK_EQ(c.offset, -6);
        night_and_day(&c, 600, false);
    }
    CHECK_EQ(c.offset, -6);                         /* the morning after the last E4 day does not count */
    night_and_day(&c, 600, false);
    CHECK_EQ(c.offset, -5);
    for (int i = 0; i < 5; i++) {
        night_and_day(&c, 600, false);
    }
    CHECK_EQ(c.offset, -5);
    night_and_day(&c, 600, true);                   /* an E4 (not after a cooled night) starts the count again */
    CHECK_EQ(c.offset, -5);
    for (int i = 0; i < 7; i++) {
        night_and_day(&c, 600, false);
    }
    CHECK_EQ(c.offset, -5);
    night_and_day(&c, 600, false);
    CHECK_EQ(c.offset, -4);

    /* never above the computed target, never below -20 K */
    sh_cool_t z = { 0 };
    for (int i = 0; i < 30; i++) {
        night_and_day(&z, 600, false);
    }
    CHECK_EQ(z.offset, 0);
    for (int i = 0; i < 10; i++) {
        night_and_day(&z, 720, true);
    }
    CHECK_EQ(z.offset, SH_COOL_OFFSET_MIN);

    /* the change is announced for storing */
    sh_cool_t e = { 0 };
    step_at(&e, HM(4, 0), 720);
    step_at(&e, HM(7, 0), 720);
    CHECK_EQ(e.events & SH_COOL_EVT_LEARNED, SH_COOL_EVT_LEARNED);     /* quiet 0 -> 1 */
    in = cin(HM(13, 0), 950);
    in.e4 = true;
    sh_cool_step(&e, &in, 0);
    CHECK_EQ(e.events & SH_COOL_EVT_LEARNED, SH_COOL_EVT_LEARNED);
    CHECK_EQ(e.offset, -3);
}

/* ---- the pump rule of the cooling (3.2) ---------------------------------------------------- */

static void test_pump_cooling(void)
{
    sh_pump_params_t p = P;
    sh_pump_state_t st = { 0 };

    /* not allowed: nothing happens, however cold the collector is */
    sh_pump_decide(&p, &st, 200, 650, 1000);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_DIFF);

    /* allowed: on when the collector is colder than the tank by the switch-on difference (6 K) */
    p.cool = true;
    sh_pump_decide(&p, &st, 591, 650, 1000);        /* 5.9 K */
    CHECK(!st.on);
    sh_pump_decide(&p, &st, 590, 650, 60);          /* 6 K, but the minimum off time is not over */
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_MIN_OFF);
    sh_pump_decide(&p, &st, 590, 650, 120);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);

    /* off below the switch-off difference (4 K), after the minimum run time */
    sh_pump_decide(&p, &st, 609, 650, 10);          /* 4.1 K */
    CHECK(st.on);
    sh_pump_decide(&p, &st, 610, 650, 179);         /* 4 K: the minimum run time holds it */
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);
    sh_pump_decide(&p, &st, 610, 650, 180);
    CHECK(!st.on);
    CHECK(!st.cool);
    /* a new enough difference starts it again */
    sh_pump_decide(&p, &st, 560, 640, 120);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);

    /* the session ends (target, morning): the pump stops after the minimum run time */
    p.cool = false;
    sh_pump_decide(&p, &st, 560, 640, 100);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_MIN_RUN);
    sh_pump_decide(&p, &st, 560, 640, 180);
    CHECK(!st.on);

    /* above Tmax the cooling runs (the tank is what it is for); normal charging does not */
    p.cool = true;
    st = (sh_pump_state_t){ 0 };
    sh_pump_decide(&p, &st, 300, 750, 1000);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);
    p.cool = false;                                 /* ended at Tmax: off at once */
    sh_pump_decide(&p, &st, 300, 750, 10);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_MAX);

    /* the emergency ceiling stops the pump only while the collector is hot too (>= 95 °C, until below
     * 93 °C): with a colder collector the cooling takes heat out of the tank — the answer to an E4 */
    p.cool = true;
    st = (sh_pump_state_t){ 0 };
    sh_pump_decide(&p, &st, 300, 940, 1000);
    CHECK(st.on);
    sh_pump_decide(&p, &st, 300, 950, 10);          /* the ceiling reached while cooling: goes on */
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);
    CHECK(st.tank_ceiling);
    sh_pump_decide(&p, &st, 950, 1000, 10);         /* the collector at 95 °C: off at once */
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_CEILING);
    CHECK(!st.cool);
    sh_pump_decide(&p, &st, 930, 1000, 1000);       /* 7 K colder, but not below 93 °C yet */
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_CEILING);
    sh_pump_decide(&p, &st, 929, 1000, 1000);       /* 92.9 °C: the cooling starts */
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);
    sh_pump_decide(&p, &st, 949, 1000, 10);         /* rising, below 95 °C: goes on */
    CHECK(st.on);
    sh_pump_decide(&p, &st, 950, 1000, 10);         /* 95 °C: off at once (minimum run time or not) */
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_CEILING);
    st = (sh_pump_state_t){ 0 };                    /* a stopped pump at the ceiling, at night */
    sh_pump_decide(&p, &st, 900, 955, 1000);        /* 5.5 K: below the switch-on difference */
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_MAX);
    sh_pump_decide(&p, &st, 895, 955, 100);         /* 6 K, minimum off time not over */
    CHECK(!st.on);
    sh_pump_decide(&p, &st, 895, 955, 180);         /* 6 K: the cooling starts above the ceiling */
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);
    sh_pump_decide(&p, &st, 915, 955, 180);         /* 4 K after the minimum run time: off */
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_DIFF);
    CHECK(!st.cool);
    /* at the ceiling with a cold collector the frost protection runs as usual (it is above) */
    st = (sh_pump_state_t){ 0 };
    sh_pump_decide(&p, &st, 30, 955, 1000);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_FROST);
    p.cool = false;                                 /* no cooling allowed: Tmax, no charging */
    st = (sh_pump_state_t){ 0 };
    sh_pump_decide(&p, &st, 300, 950, 1000);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_MAX);
    p.cool = true;
    sh_pump_decide(&p, &st, 300, 931, 1000);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);
    sh_pump_decide(&p, &st, 300, 929, 1000);
    CHECK(st.on);
    CHECK(!st.tank_ceiling);
    /* the boiling point and a faulty sensor stay above it */
    sh_pump_decide(&p, &st, SH_TEMP_INVALID, 929, 10);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_SENSOR);
    /* the frost protection wins (it runs the pump too) */
    st = (sh_pump_state_t){ 0 };
    sh_pump_decide(&p, &st, 30, 600, 1000);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_FROST);

    /* while the cooling is allowed the day can still charge (sunrise before the end of the window) */
    st = (sh_pump_state_t){ 0 };
    sh_pump_decide(&p, &st, 500, 400, 1000);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_DIFF);
}

/* ---- vacation (3.3) ------------------------------------------------------------------------ */

static void test_vacation(void)
{
    /* by day: no normal charging; the collector protection at the vacation limit (90 °C) */
    sh_pump_params_t p = P;
    p.charge_off = true;
    p.col_protect = 900;
    sh_pump_state_t st = { 0 };
    sh_pump_decide(&p, &st, 800, 400, 1000);        /* 40 K difference: not charged */
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_VACATION);
    sh_pump_decide(&p, &st, 899, 400, 1000);
    CHECK(!st.on);
    sh_pump_decide(&p, &st, 900, 400, 0);           /* the limit: at once */
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COL_PROTECT);
    sh_pump_decide(&p, &st, 879, 400, 179);         /* below the limit - 2 K, minimum run time not over */
    CHECK(st.on);
    sh_pump_decide(&p, &st, 880, 400, 180);         /* not below 88 °C yet */
    CHECK(st.on);
    sh_pump_decide(&p, &st, 879, 400, 180);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_VACATION);
    /* the emergency ceiling holds */
    sh_pump_decide(&p, &st, 950, 949, 0);
    CHECK(st.on);
    sh_pump_decide(&p, &st, 950, 950, 10);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_CEILING);
    /* a pump found charging when the vacation is switched on stops at once */
    st = (sh_pump_state_t){ .on = true, .reason = SH_PUMP_REASON_DIFF };
    sh_pump_decide(&p, &st, 800, 400, 10);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_VACATION);

    /* at night: on only when the collector is below the tank - 6 K, off below 4 K; no target */
    p.cool = true;
    st = (sh_pump_state_t){ 0 };
    sh_pump_decide(&p, &st, 345, 400, 1000);        /* 5.5 K */
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_VACATION);
    sh_pump_decide(&p, &st, 340, 400, 1000);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);
    sh_pump_decide(&p, &st, 250, 300, 5000);        /* far below any target of a) / b): still cooling */
    CHECK(st.on);
    sh_pump_decide(&p, &st, 261, 300, 5000);        /* 3.9 K: equal enough */
    CHECK(!st.on);
    /* the morning: the session is over, the pump stops at once (no charging in the vacation) */
    st = (sh_pump_state_t){ 0 };
    sh_pump_decide(&p, &st, 200, 400, 1000);
    CHECK(st.on);
    p.cool = false;
    sh_pump_decide(&p, &st, 200, 400, 10);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_VACATION);

    /* the sessions: the whole night, from the sunset to the sunrise */
    sh_cool_t c = { 0 };
    sh_cool_in_t in = cin(HM(22, 0), 500);
    in.vacation = true;
    in.night = false;
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    in.night = true;
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_VACATION);
    CHECK_EQ(c.events, SH_COOL_EVT_START);
    in.min = HM(1, 0);
    in.t_tank = 200;                                /* no target: it goes on */
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_VACATION);
    in.min = HM(5, 30);
    in.night = false;                               /* sunrise */
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    CHECK_EQ(c.end, SH_COOL_END_TIME);

    /* a) / b) do not run in the vacation; switching it off ends its night at once */
    in = cin(HM(4, 30), 720);
    in.vacation = true;
    in.night = true;
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_VACATION);
    in.vacation = false;
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.end, SH_COOL_END_MODE);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_TMAX);              /* normal mode again: the tank is above Tmax, 04:30 */
    in.vacation = true;                             /* and back */
    sh_cool_step(&c, &in, 0);
    CHECK_EQ(c.trigger, SH_COOL_VACATION);
    CHECK_EQ(c.events, SH_COOL_EVT_START | SH_COOL_EVT_END);

    /* the heater in the vacation: no cooling for the rest of the night */
    in.pump_cool = true;
    sh_cool_step(&c, &in, 0);
    in.t_tank = 726;
    sh_cool_step(&c, &in, 0);
    CHECK(c.heater);
    CHECK_EQ(c.trigger, SH_COOL_NONE);
    CHECK_EQ(c.end, SH_COOL_END_HEATER);
}

/* ---- sunrise and sunset (3.2) -------------------------------------------------------------- */

typedef struct {
    sh_sun_t s;
    uint32_t now;
    sh_local_t lt;
    bool reliable;
} sun_t;

static sun_t sun_at(uint16_t day, uint16_t min)
{
    sun_t u;
    memset(&u, 0, sizeof(u));
    sh_sun_days_reset(&u.s.days);
    u.lt = (sh_local_t){ .set = true, .day = day, .min = min };
    u.reliable = true;
    return u;
}

/* one minute later */
static uint8_t sun_min(sun_t *u, int16_t col, int16_t tank, bool pump)
{
    u->now += 60;
    if (u->lt.set && ++u->lt.min == SH_DAY_MIN) {
        u->lt.min = 0;
        u->lt.day++;
    }
    return sh_sun_step(&u->s, col, tank, pump, &u->lt, u->reliable, u->now);
}

static void test_sunrise(void)
{
    /* 2 K in 10 min with the pump stopped, the first time after midnight */
    sun_t u = sun_at(2500, HM(5, 0));
    for (int i = 0; i < 30; i++) {
        CHECK_EQ(sun_min(&u, 100, 400, false), 0);
    }
    int16_t col = 100;
    for (int i = 0; i < 9; i++) {
        col += 2;                                   /* 0.2 K a minute */
        CHECK_EQ(sun_min(&u, col, 400, false), 0);
    }
    col += 1;                                       /* 1.9 K over the last 10 min */
    CHECK_EQ(sun_min(&u, col, 400, false), 0);
    col += 3;                                       /* 2.0 K (from the sample of 10 min ago: 10.2 °C) */
    CHECK_EQ(sun_min(&u, col, 400, false), SH_SUN_EVT_RISE | SH_SUN_EVT_LEARNED);
    CHECK_EQ(u.s.days.rise[SH_SUN_DAYS - 1], HM(5, 41));
    for (int i = 0; i < 30; i++) {                  /* only once a day */
        col += 5;
        CHECK_EQ(sun_min(&u, col, 400, false) & SH_SUN_EVT_RISE, 0);
    }

    /* a running pump: no judgement, and 10 min with the pump stopped are needed again */
    sun_t p = sun_at(2500, HM(5, 0));
    col = 100;
    for (int i = 0; i < 20; i++) {
        col += 10;
        CHECK_EQ(sun_min(&p, col, 400, true), 0);
    }
    for (int i = 0; i < 10; i++) {
        col += 10;
        CHECK_EQ(sun_min(&p, col, 400, false), 0);
    }
    col += 10;
    CHECK_EQ(sun_min(&p, col, 400, false) & SH_SUN_EVT_RISE, SH_SUN_EVT_RISE);

    /* the next day has its own sunrise; a rise after noon (restart in the afternoon) is none */
    sun_t n = sun_at(2500, HM(13, 0));
    col = 300;
    for (int i = 0; i < 30; i++) {
        col += 10;
        CHECK_EQ(sun_min(&n, col, 900, false), 0);
    }
    CHECK_EQ(sh_sun_avg(n.s.days.rise), -1);
    n.lt = (sh_local_t){ .set = true, .day = 2501, .min = HM(6, 0) };
    col = 100;
    uint8_t evt = 0;
    for (int i = 0; i < 12; i++) {
        col += 10;
        evt |= sun_min(&n, col, 900, false);
    }
    CHECK_EQ(evt, SH_SUN_EVT_RISE | SH_SUN_EVT_LEARNED);
    CHECK_EQ(sh_sun_avg(n.s.days.rise), HM(6, 11));

    /* not reliable (or no clock): detected, but not learned */
    sun_t x = sun_at(2500, HM(5, 0));
    x.reliable = false;
    col = 100;
    evt = 0;
    for (int i = 0; i < 12; i++) {
        col += 10;
        evt |= sun_min(&x, col, 900, false);
    }
    CHECK_EQ(evt, SH_SUN_EVT_RISE);
    CHECK_EQ(sh_sun_avg(x.s.days.rise), -1);

    /* a faulty collector sensor: nothing */
    sun_t f = sun_at(2500, HM(5, 0));
    for (int i = 0; i < 30; i++) {
        CHECK_EQ(sun_min(&f, SH_TEMP_INVALID, 400, false), 0);
    }
}

static void test_sunset(void)
{
    /* a day in which the collector was never above the tank has no sunset */
    sun_t u = sun_at(2500, HM(10, 0));
    for (int i = 0; i < 600; i++) {
        CHECK_EQ(sun_min(&u, 300, 400, false), 0);
    }
    CHECK(!u.s.dark);

    /* above the tank, then below it with the pump stopped */
    u = sun_at(2500, HM(17, 0));
    CHECK_EQ(sun_min(&u, 600, 500, true), 0);
    CHECK_EQ(sun_min(&u, 500, 500, false), 0);      /* equal: not below */
    CHECK_EQ(sun_min(&u, 499, 500, true), 0);       /* the pump runs: not yet */
    CHECK_EQ(sun_min(&u, 499, 500, false), SH_SUN_EVT_SET);
    CHECK(u.s.dark);
    CHECK_EQ(u.s.sunset_s, u.now);
    CHECK_EQ(sun_min(&u, 450, 500, false), 0);      /* once */
    CHECK_EQ(sh_sun_avg(u.s.days.set), -1);         /* the day is not over: a later one may replace it */

    /* a cloud, then the sun again: day again; the last fall of the day is the sunset */
    CHECK_EQ(sun_min(&u, 520, 500, false), 0);
    CHECK(!u.s.dark);
    for (int i = 0; i < 60; i++) {
        sun_min(&u, 520, 500, false);
    }
    CHECK_EQ(sun_min(&u, 480, 500, false), SH_SUN_EVT_SET);
    const uint16_t last = u.lt.min;
    CHECK_EQ(last, HM(18, 7));
    CHECK(u.s.dark);
    uint8_t evt = 0;
    while (u.lt.day == 2500) {
        evt |= sun_min(&u, 300, 480, false);
    }
    CHECK_EQ(evt, SH_SUN_EVT_LEARNED);              /* midnight: learned */
    CHECK_EQ(u.s.days.set[SH_SUN_DAYS - 1], last);
    CHECK_EQ(sh_sun_avg(u.s.days.set), last);
    CHECK(u.s.dark);                                /* dark until the sunrise */

    /* the sunrise ends the dark */
    int16_t col = 100;
    u.lt.min = HM(5, 30);
    evt = 0;
    for (int i = 0; i < 12; i++) {
        col += 10;
        evt |= sun_min(&u, col, 480, false);
    }
    CHECK_EQ(evt & SH_SUN_EVT_RISE, SH_SUN_EVT_RISE);
    CHECK(!u.s.dark);

    /* without a clock: the first rise after the sunset is the sunrise, each night */
    sun_t n;
    memset(&n, 0, sizeof(n));
    sh_sun_days_reset(&n.s.days);
    for (int night = 0; night < 2; night++) {
        sun_min(&n, 600, 500, false);
        CHECK_EQ(sun_min(&n, 400, 500, false), SH_SUN_EVT_SET);
        col = 100;
        for (int i = 0; i < 300; i++) {
            CHECK_EQ(sun_min(&n, col, 500, false), 0);
        }
        CHECK(n.s.dark);
        evt = 0;
        for (int i = 0; i < 12; i++) {
            col += 10;
            evt |= sun_min(&n, col, 500, false);
        }
        CHECK_EQ(evt, SH_SUN_EVT_RISE);
        CHECK(!n.s.dark);
    }
    CHECK_EQ(sh_sun_avg(n.s.days.set), -1);         /* nothing learned without a clock */
}

/* The learned times: the average of the last 7 detected; the night by them or by this night. */
static void test_sun_learned(void)
{
    sh_sun_days_t d;
    sh_sun_days_reset(&d);
    CHECK_EQ(sh_sun_avg(d.rise), -1);
    d.rise[6] = 360;
    CHECK_EQ(sh_sun_avg(d.rise), 360);
    d.rise[5] = 371;
    CHECK_EQ(sh_sun_avg(d.rise), 366);              /* 365.5 */
    for (int i = 0; i < SH_SUN_DAYS; i++) {
        d.set[i] = (uint16_t)(1200 + i);
    }
    CHECK_EQ(sh_sun_avg(d.set), 1203);

    /* 9 sunrises, a day apart: the last 7 are kept */
    sun_t u = sun_at(2500, 0);
    for (int day = 0; day < 9; day++) {
        u.lt = (sh_local_t){ .set = true, .day = (uint16_t)(2500 + day), .min = (uint16_t)(HM(6, 0) + day) };
        int16_t col = 100;
        for (int i = 0; i < 12; i++) {
            col += 10;
            sun_min(&u, col, 900, false);
        }
        for (int i = 0; i < 30; i++) {
            sun_min(&u, col, 900, true);            /* (the pump: the next rise needs 10 min again) */
        }
    }
    CHECK_EQ(u.s.days.rise[0], HM(6, 11) + 2);
    CHECK_EQ(u.s.days.rise[6], HM(6, 11) + 8);
    CHECK_EQ(sh_sun_avg(u.s.days.rise), HM(6, 11) + 5);

    /* night: with a reliable clock and both averages by the minute, otherwise by this night */
    sh_sun_t s;
    memset(&s, 0, sizeof(s));
    sh_sun_days_reset(&s.days);
    sh_local_t lt = { .set = true, .day = 2500, .min = HM(23, 0) };
    CHECK(!sh_sun_night(&s, &lt, true));            /* nothing learned, no sunset seen */
    s.dark = true;
    CHECK(sh_sun_night(&s, &lt, true));
    s.days.rise[6] = HM(6, 0);
    s.days.set[6] = HM(20, 0);
    s.dark = false;
    lt.min = HM(19, 59);
    CHECK(!sh_sun_night(&s, &lt, true));
    lt.min = HM(20, 0);
    CHECK(sh_sun_night(&s, &lt, true));
    lt.min = HM(5, 59);
    CHECK(sh_sun_night(&s, &lt, true));
    lt.min = HM(6, 0);
    CHECK(!sh_sun_night(&s, &lt, true));
    lt.min = HM(23, 0);
    CHECK(!sh_sun_night(&s, &lt, false));           /* not reliable: only what was detected */
    s.dark = true;
    lt.min = HM(12, 0);
    CHECK(sh_sun_night(&s, &lt, false));
    lt.set = false;
    CHECK(sh_sun_night(&s, &lt, true));
}

/* ---- seasons (3.4) ------------------------------------------------------------------------- */

typedef struct {
    sh_season_t s;
    uint32_t now;
    bool winter;
} season_t;

/* One period of 24 h by the collector: its lowest night temperature and, if `hot_run_s`, a pump
 * run of that length ending at a collector of `hot_col`. */
static void period(season_t *u, int16_t night_min, uint32_t hot_run_s, int16_t hot_col)
{
    const uint32_t start = u->now;
    for (uint32_t t = 0; t < SH_SEASON_PERIOD_S; t += 60) {
        u->now = start + t;
        const bool run = t >= 39600 && t < 39600 + hot_run_s;
        const bool run_end = hot_run_s && t + 60 >= 39600 + hot_run_s && run;
        const int16_t col = t == 9600 ? night_min : run_end ? hot_col : (int16_t)(night_min + 100);
        u->winter = sh_season_step(&u->s, SH_SEASON_AUTO, u->winter, col, run, false, false, u->now);
    }
    u->now = start + SH_SEASON_PERIOD_S;
    u->winter = sh_season_step(&u->s, SH_SEASON_AUTO, u->winter, (int16_t)(night_min + 100), false, false, false,
                               u->now);
}

static void test_season(void)
{
    sh_season_t s = { 0 };
    /* by hand: never changed, whatever the collector and the clock say */
    CHECK(!sh_season_step(&s, SH_SEASON_SUMMER, false, -200, false, true, false, 0));
    CHECK(!sh_season_step(&s, SH_SEASON_SUMMER, true, -200, false, false, false, 10));
    CHECK(sh_season_step(&s, SH_SEASON_WINTER, false, 900, true, true, true, 20));

    /* automatic, reliable clock in a zone with daylight saving time: by it */
    CHECK(!sh_season_step(&s, SH_SEASON_AUTO, true, -200, false, true, true, 30));      /* summer time */
    CHECK(sh_season_step(&s, SH_SEASON_AUTO, false, 900, true, true, false, 40));       /* winter time */

    /* automatic, by the collector: the season kept from before stays at first (summer at first) */
    season_t u = { .winter = false };
    period(&u, 80, 0, 0);
    CHECK(!u.winter);

    /* summer -> winter: the lowest night temperature below 5 °C, 3 nights in a row */
    period(&u, 49, 0, 0);
    period(&u, 49, 0, 0);
    CHECK(!u.winter);
    period(&u, 50, 0, 0);                           /* 5.0 °C is not below: the row starts again */
    CHECK(!u.winter);
    period(&u, 49, 0, 0);
    period(&u, -30, 0, 0);
    CHECK(!u.winter);
    period(&u, 20, 0, 0);
    CHECK(u.winter);

    /* winter -> summer: collector above 60 °C with the pump running without a break for over 1 h,
     * 3 days in a row; cold nights do not matter any more */
    period(&u, -50, 3600 + 120, 601);
    period(&u, -50, 3600 + 120, 601);
    CHECK(u.winter);
    period(&u, -50, 3600 + 120, 600);               /* 60.0 °C is not above */
    CHECK(u.winter);
    period(&u, -50, 3600 + 120, 700);
    period(&u, -50, 3600 + 60, 700);                /* the pump ran 1 h, not longer */
    CHECK(u.winter);
    period(&u, -50, 5000, 650);
    period(&u, -50, 5000, 650);
    CHECK(u.winter);
    period(&u, -50, 5000, 650);
    CHECK(!u.winter);

    /* only from the current season to the other: hot days in summer, cold nights in winter change
     * nothing, and both signs in one period do not fight */
    period(&u, 100, 5000, 900);
    period(&u, 100, 5000, 900);
    period(&u, 100, 5000, 900);
    CHECK(!u.winter);
    for (int i = 0; i < 3; i++) {
        period(&u, 0, 5000, 900);                   /* a cold night and a hot day: summer -> winter */
    }
    CHECK(u.winter);
    for (int i = 0; i < 2; i++) {
        period(&u, 0, 5000, 900);
    }
    CHECK(u.winter);
    period(&u, 0, 5000, 900);                       /* three more: winter -> summer */
    CHECK(!u.winter);

    /* a faulty collector sensor: such a period is not a sign */
    season_t f = { .winter = false };
    for (int i = 0; i < 5; i++) {
        f.winter = sh_season_step(&f.s, SH_SEASON_AUTO, f.winter, SH_TEMP_INVALID, false, false, false,
                                  (uint32_t)i * SH_SEASON_PERIOD_S);
    }
    CHECK(!f.winter);

    /* the clock takes over and gives back: the observations start over */
    season_t c = { .winter = false };
    period(&c, 0, 0, 0);
    period(&c, 0, 0, 0);
    c.winter = sh_season_step(&c.s, SH_SEASON_AUTO, c.winter, 0, false, true, true, c.now + 1);
    CHECK(!c.winter);
    c.now += 2;
    period(&c, 0, 0, 0);
    CHECK(!c.winter);                               /* 1 in a row, not 3 */
    period(&c, 0, 0, 0);
    period(&c, 0, 0, 0);
    CHECK(c.winter);
}

/* ---- air venting (3.5) --------------------------------------------------------------------- */

static void test_air_venting(void)
{
    const sh_pump_temps_t cold = { .t_col = 300, .t_tank = 400 };      /* nothing to charge */
    sh_pump_ctl_t c = { 0 };
    sh_air_view_t v;
    uint32_t now = 5000;

    /* 3 cycles of 5 min run + 10 min pause (water) */
    sh_pump_ctl_air_start(&c, 600, now);
    CHECK_EQ(c.events, SH_PUMP_EVT_AIR_START);
    c.events = 0;
    for (unsigned cycle = 0; cycle < SH_AIR_CYCLES; cycle++) {
        const uint32_t t0 = now + cycle * 900;
        sh_pump_ctl_step(&c, &P, &cold, t0);
        CHECK(c.on);
        CHECK_EQ(c.reason, SH_PUMP_REASON_AIR_RUN);
        sh_pump_ctl_air(&c, t0, &v);
        CHECK(v.on && v.run);
        CHECK_EQ(v.cycle, cycle + 1);
        CHECK_EQ(v.left_s, 2700 - cycle * 900);
        sh_pump_ctl_step(&c, &P, &cold, t0 + 299);
        CHECK(c.on);
        sh_pump_ctl_step(&c, &P, &cold, t0 + 300);
        CHECK(!c.on);
        CHECK_EQ(c.reason, SH_PUMP_REASON_AIR_PAUSE);
        sh_pump_ctl_air(&c, t0 + 300, &v);
        CHECK(v.on && !v.run);
        CHECK_EQ(v.cycle, cycle + 1);
        sh_pump_ctl_step(&c, &P, &cold, t0 + 899);
        CHECK(!c.on);
        CHECK_EQ(c.reason, SH_PUMP_REASON_AIR_PAUSE);
        CHECK_EQ(c.events, 0);
    }
    sh_pump_ctl_step(&c, &P, &cold, now + 2700);    /* over */
    CHECK(!c.on);
    CHECK(!c.air);
    CHECK_EQ(c.events, SH_PUMP_EVT_AIR_END);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    sh_pump_ctl_air(&c, now + 2700, &v);
    CHECK(!v.on);
    CHECK_EQ(v.left_s, 0);

    /* the pause by the fluid: 25 min with 30 % glycol; no charging in the pause, however hot */
    const sh_pump_temps_t sunny = { .t_col = 800, .t_tank = 400 };
    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_air_start(&c, 1500, 100);
    sh_pump_ctl_step(&c, &P, &sunny, 100 + 300);
    CHECK(!c.on);
    sh_pump_ctl_step(&c, &P, &sunny, 100 + 1799);
    CHECK(!c.on);
    sh_pump_ctl_step(&c, &P, &sunny, 100 + 1800);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_AIR_RUN);
    /* "Stop" / short "menu": ends at once; the normal control goes on (here: charging) */
    c.events = 0;
    sh_pump_ctl_air_stop(&c);
    sh_pump_ctl_step(&c, &P, &sunny, 100 + 1900);
    CHECK(!c.air);
    CHECK_EQ(c.events, SH_PUMP_EVT_AIR_END);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);

    /* only the boiling point and the tank ceiling stop it — and end it (like the test) */
    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_air_start(&c, 600, 100);
    const sh_pump_temps_t above_tmax = { .t_col = 300, .t_tank = 800 };
    sh_pump_ctl_step(&c, &P, &above_tmax, 100);     /* Tmax does not matter */
    CHECK(c.on);
    const sh_pump_temps_t ceiling_cold = { .t_col = 300, .t_tank = 950 };
    sh_pump_ctl_step(&c, &P, &ceiling_cold, 130);   /* the collector cold: goes on */
    CHECK(c.on);
    CHECK(c.air);
    const sh_pump_temps_t ceiling = { .t_col = 950, .t_tank = 950 };
    c.events = 0;
    sh_pump_ctl_step(&c, &P, &ceiling, 160);
    CHECK(!c.on);
    CHECK(!c.air);
    CHECK_EQ(c.events, SH_PUMP_EVT_AIR_END);
    CHECK_EQ(c.reason, SH_PUMP_REASON_TANK_CEILING);
    sh_pump_ctl_step(&c, &P, &cold, 5000);          /* it does not come back by itself */
    CHECK(!c.air);
    CHECK(!c.on);

    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_air_start(&c, 600, 100);
    sh_pump_ctl_step(&c, &P, &cold, 100);
    const sh_pump_temps_t boiling = { .t_col = 1277, .t_tank = 400 };
    sh_pump_ctl_step(&c, &P, &boiling, 130);
    CHECK(!c.on);
    CHECK(!c.air);
    CHECK_EQ(c.reason, SH_PUMP_REASON_COL_BOILING);
    c.events = 0;                                   /* asked for while the limit is active: refused */
    sh_pump_ctl_air_start(&c, 600, 140);
    sh_pump_ctl_step(&c, &P, &boiling, 140);
    CHECK(!c.air);
    CHECK_EQ(c.events, SH_PUMP_EVT_AIR_START | SH_PUMP_EVT_AIR_END);

    /* the venting and the pump test do not run together */
    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_air_start(&c, 600, 100);
    sh_pump_ctl_plus(&c, 300, 110);
    CHECK(!c.test);
    sh_pump_ctl_step(&c, &P, &cold, 110);
    CHECK_EQ(c.reason, SH_PUMP_REASON_AIR_RUN);
    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_plus(&c, 300, 100);
    sh_pump_ctl_air_start(&c, 600, 110);
    CHECK(!c.air);
    sh_pump_ctl_step(&c, &P, &cold, 110);
    CHECK_EQ(c.reason, SH_PUMP_REASON_TEST);

    /* it works with a faulty sensor too (like the test) */
    const sh_pump_temps_t fault = { .t_col = 300, .t_tank = 400, .col_fault = true, .tank_fault = true };
    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_air_start(&c, 600, 100);
    sh_pump_ctl_step(&c, &P, &fault, 100);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_AIR_RUN);
}

/* ---- the cooling in the pump control: priorities (3.7) ------------------------------------- */

static void test_ctl_priorities(void)
{
    sh_pump_params_t p = P;
    p.cool = true;
    const sh_pump_temps_t night = { .t_col = 200, .t_tank = 720 };
    sh_pump_ctl_t c = { 0 };
    sh_pump_ctl_step(&c, &p, &night, 1000);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_COOL);

    /* the pump test is above it and the cooling goes on after it */
    sh_pump_ctl_plus(&c, 60, 1100);
    sh_pump_ctl_step(&c, &p, &night, 1100);
    CHECK_EQ(c.reason, SH_PUMP_REASON_TEST);
    sh_pump_ctl_step(&c, &p, &night, 1160);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_COOL);

    /* a faulty sensor: no cooling (the fault modes decide) */
    const sh_pump_temps_t tank_fault = { .t_col = 200, .t_tank = 720, .tank_fault = true };
    sh_pump_ctl_step(&c, &p, &tank_fault, 2000);
    CHECK(c.reason != SH_PUMP_REASON_COOL);
    CHECK(!c.norm.cool);
    const sh_pump_temps_t col_fault = { .t_col = 200, .t_tank = 720, .col_fault = true };
    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_step(&c, &p, &col_fault, 3000);
    CHECK(c.reason != SH_PUMP_REASON_COOL);

    /* the collector protection (also the one of the vacation) is above the cooling */
    c = (sh_pump_ctl_t){ 0 };
    const sh_pump_temps_t hot = { .t_col = 1100, .t_tank = 1200 };     /* (not real: both at once) */
    sh_pump_ctl_step(&c, &p, &hot, 4000);           /* the ceiling with the collector hot */
    CHECK_EQ(c.reason, SH_PUMP_REASON_TANK_CEILING);
    const sh_pump_temps_t hot2 = { .t_col = 1210, .t_tank = 1200 };
    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_step(&c, &p, &hot2, 4000);
    CHECK_EQ(c.reason, SH_PUMP_REASON_TANK_CEILING);
    const sh_pump_temps_t prot = { .t_col = 1100, .t_tank = 600 };
    c = (sh_pump_ctl_t){ 0 };
    sh_pump_ctl_step(&c, &p, &prot, 4000);
    CHECK_EQ(c.reason, SH_PUMP_REASON_COL_PROTECT);
}

/* ---- indication, display order, the day ---------------------------------------------------- */

static void test_indication(void)
{
    const sh_ind_params_t ip = { .cold = 200, .hot = 900 };
    sh_ind_state_t st = { 0 };
    sh_ind_in_t in = { .t_col = 300, .wifi_connected = true };

    /* the pump cools the tank at night: orange, no code */
    in.pump_on = true;
    in.cooling = true;
    CHECK_EQ(sh_ind_update(&st, &ip, &in).color, SH_COLOR_ORANGE);
    in.pump_on = false;                             /* waiting for the difference: not orange */
    CHECK_EQ(sh_ind_update(&st, &ip, &in).color, SH_COLOR_BLUE);

    /* vacation: purple; above the yellow of a hot collector, below orange and red */
    in = (sh_ind_in_t){ .t_col = 300, .wifi_connected = true, .vacation = true };
    CHECK_EQ(sh_ind_update(&st, &ip, &in).color, SH_COLOR_PURPLE);
    CHECK_EQ(sh_ind_update(&st, &ip, &in).pattern, SH_IND_STEADY);
    in.t_col = 950;
    CHECK_EQ(sh_ind_update(&st, &ip, &in).color, SH_COLOR_PURPLE);
    in.pump_on = true;
    in.alarms = SH_ALARM_BIT(SH_ALARM_HOT);         /* the protection of the vacation works */
    CHECK_EQ(sh_ind_update(&st, &ip, &in).color, SH_COLOR_ORANGE);
    in.alarms = 0;
    in.t_col = 200;
    in.cooling = true;                              /* the night of the vacation */
    CHECK_EQ(sh_ind_update(&st, &ip, &in).color, SH_COLOR_ORANGE);
    in.cooling = false;
    in.pump_on = false;
    in.alarms = SH_ALARM_BIT(SH_ALARM_E4);
    CHECK_EQ(sh_ind_update(&st, &ip, &in).color, SH_COLOR_RED);
    in.alarms = 0;
    in.wifi_connected = false;
    CHECK_EQ(sh_ind_update(&st, &ip, &in).color, SH_COLOR_PURPLE);
    CHECK_EQ(sh_ind_update(&st, &ip, &in).pattern, SH_IND_HEARTBEAT);

    /* "HoL" follows the codes on the display: its place is the number of the active codes */
    CHECK_EQ(sh_alarm_count(0), 0);
    CHECK_EQ(sh_alarm_count(SH_ALARM_BIT(SH_ALARM_E4) | SH_ALARM_BIT(SH_ALARM_HOT)), 2);
    CHECK_EQ(sh_alarm_count(0xFF), SH_ALARM_COUNT);
    const uint8_t two = SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_FRO);
    CHECK_EQ(sh_alarm_code_at(two, 1 * SH_ALARM_CODE_MS), SH_ALARM_FRO);
    CHECK_EQ(sh_alarm_code_at(two, sh_alarm_count(two) * SH_ALARM_CODE_MS), SH_ALARM_COUNT);
    /* the cooling itself has no code */
    sh_alarm_state_t as = { 0 };
    const sh_alarm_params_t ap = { .e6_time_s = 600, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200,
                                   .e6_clear_diff = 100 };
    const sh_alarm_in_t ain = { .t_col = 200, .t_tank = 700, .pump_on = true, .reason = SH_PUMP_REASON_COOL };
    for (uint32_t t = 0; t < 3600; t += 60) {
        CHECK_EQ(sh_alarm_step(&as, &ap, &ain, t), 0);
    }
}

static void test_day(void)
{
    /* the kinds: the cooling has its own, the venting is "other" */
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_COOL), SH_RUN_COOL);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_AIR_RUN), SH_RUN_OTHER);
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_AIR_PAUSE), -1);
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_VACATION), -1);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_COL_PROTECT), SH_RUN_PROTECT);

    /* a day: 40 min of cooling at night, a sunrise, two sunsets (the last one counts) */
    sh_day_t d;
    sh_day_init(&d);
    sh_day_sum_t done;
    sh_local_t lt = { .set = true, .day = 2500, .min = 0 };
    sh_day_in_t in = { .t_col = 200, .t_tank = 700 };
    uint32_t now = 100000;
    bool over = false;
    for (int min = 0; min < 2 * SH_DAY_MIN && !over; min++, now += 60) {
        lt.day = (uint16_t)(2500 + min / SH_DAY_MIN);
        lt.min = (uint16_t)(min % SH_DAY_MIN);
        in.pump_on = min >= HM(4, 0) && min < HM(4, 40);
        in.reason = SH_PUMP_REASON_COOL;
        in.sunrises = min >= HM(5, 50) ? 1 : 0;
        in.sunsets = min >= HM(19, 10) ? 2 : min >= HM(15, 30) ? 1 : 0;
        over = sh_day_step(&d, &in, &lt, now, &done);
    }
    CHECK(over);
    CHECK_EQ(done.run_s[SH_RUN_COOL], 40 * 60);
    CHECK_EQ(done.run_s[SH_RUN_CHARGE] + done.run_s[SH_RUN_PROTECT] + done.run_s[SH_RUN_OTHER], 0);
    CHECK_EQ(done.sunrise, HM(5, 50));
    CHECK_EQ(done.sunset, HM(19, 10));
    CHECK_EQ(d.sum.sunrise, -1);                    /* the new day: nothing yet */
    CHECK_EQ(d.sum.sunset, -1);

    /* the daily record keeps them: cooling minutes, sunrise, sunset, the target */
    sh_hist_rec_t r, back;
    sh_hist_rec_init(&r, done.day);
    CHECK_EQ(r.cool_min, 0);
    CHECK_EQ(r.sunrise, SH_HIST_NO_TIME);
    CHECK_EQ(r.cool_target, SH_HIST_NO_U8);
    r.cool_min = 40;
    r.sunrise = (uint16_t)done.sunrise;
    r.sunset = (uint16_t)done.sunset;
    r.cool_target = 63;
    uint8_t raw[SH_HIST_REC_SIZE];
    sh_hist_encode(&r, raw);
    CHECK_EQ(sh_hist_decode(raw, &back), SH_HIST_VALID);
    CHECK_EQ(back.cool_min, 40);
    CHECK_EQ(back.sunrise, HM(5, 50));
    CHECK_EQ(back.sunset, HM(19, 10));
    CHECK_EQ(back.cool_target, 63);
    char line[SH_HIST_CSV_LINE_MAX];
    CHECK(sh_hist_csv_line(line, sizeof(line), &r) > 0);
    CHECK(strstr(line, ";0;0;0;40;0;") != NULL);
    CHECK(strstr(line, ";05:50;19:10;63\r\n") != NULL);
}

int main(void)
{
    test_installer_fields();
    test_vacation_limit();
    test_air_pause();
    test_cool_target();
    test_trigger_tmax();
    test_trigger_e4();
    test_no_clock();
    test_heater();
    test_learning();
    test_pump_cooling();
    test_vacation();
    test_sunrise();
    test_sunset();
    test_sun_learned();
    test_season();
    test_air_venting();
    test_ctl_priorities();
    test_indication();
    test_day();
    return test_summary("test_group_c");
}
