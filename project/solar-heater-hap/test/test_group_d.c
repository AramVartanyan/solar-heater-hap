/* Host test of group D (doc/tasks/step12-heater-leak-e8.md 3.1 - 3.4): the heater power in the
 * installer data, the heater sign and the heater found with "0 kW" (sh_heater, sh_pump_ctl), the
 * heater leak — daylight, schedule of the checks, their result, the new start (sh_heater,
 * sh_pump_logic, sh_pump_ctl, sh_sun) — and the reverse thermosiphon E8 (sh_e8, sh_alarm, sh_ind);
 * the heater minutes of the day (sh_day, sh_hist).
 * Temperatures are tenths of °C, times seconds; local time in minutes of the day. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_heater.h"
#include "sh_e8.h"
#include "sh_sun.h"
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
    .heater_rise = SH_HEATER_RISE_MIN,
};

/* ---- installer data: the heater power (3.1) ------------------------------------------------ */

static void test_installer_heater(void)
{
    sh_installer_t d = { .v = {
        [SH_INST_FLUID] = SH_FLUID_WATER, [SH_INST_PRESSURE] = 15, [SH_INST_COL_MAX] = 120,
        [SH_INST_COL_TYPE] = SH_COL_FLAT, [SH_INST_COL_AREA] = 20, [SH_INST_TANK_VOL] = 150,
        [SH_INST_HEATER] = 20,
    } };
    const sh_installer_t defaults = d;
    CHECK(sh_installer_ok(&d));
    CHECK(strcmp(sh_inst_info[SH_INST_HEATER].key, "heater") == 0);

    /* 0 = none, otherwise 0.5 - 12.0 kW in steps of 0.5 kW (tenths of kW) */
    static const struct { int16_t v; bool ok; } V[] = {
        { 0, true }, { 5, true }, { 10, true }, { 35, true }, { 115, true }, { 120, true },
        { -5, false }, { 1, false }, { 4, false }, { 6, false }, { 21, false }, { 121, false }, { 125, false },
    };
    for (unsigned i = 0; i < sizeof(V) / sizeof(V[0]); i++) {
        d.v[SH_INST_HEATER] = V[i].v;
        CHECK_EQ(sh_installer_ok(&d), V[i].ok);
    }
    /* a stored value out of the range: the default (2.0 kW) */
    d.v[SH_INST_HEATER] = 123;
    CHECK_EQ(sh_installer_sanitize(&d, &defaults), 1u << SH_INST_HEATER);
    CHECK_EQ(d.v[SH_INST_HEATER], 20);
    d.v[SH_INST_HEATER] = 0;
    CHECK_EQ(sh_installer_sanitize(&d, &defaults), 0);
    CHECK_EQ(d.v[SH_INST_HEATER], 0);
}

/* ---- the heater sign (3.2) ----------------------------------------------------------------- */

static void test_heater_rise(void)
{
    /* half of power / (volume x 1.163) for 10 min, at least 0.5 K */
    CHECK_EQ(sh_heater_rise(0, 150), 5);            /* "0 kW": 0.5 K */
    CHECK_EQ(sh_heater_rise(0, 50), 5);
    CHECK_EQ(sh_heater_rise(20, 150), 10);          /* 2 kW, 150 l: 1.91 K -> 1.0 K */
    CHECK_EQ(sh_heater_rise(35, 120), 21);          /* the example of the document: ~2 K */
    CHECK_EQ(sh_heater_rise(120, 150), 57);         /* 12 kW, 150 l: 11.5 K -> 5.7 K */
    CHECK_EQ(sh_heater_rise(120, 50), 172);         /* the highest: 12 kW, 50 l */
    CHECK_EQ(sh_heater_rise(5, 1000), 5);           /* the lowest: 0.5 kW, 1000 l: 0.04 K -> 0.5 K */
    CHECK_EQ(sh_heater_rise(20, 1000), 5);
    CHECK_EQ(sh_heater_rise(5, 50), 7);             /* 0.5 kW, 50 l: 1.43 K -> 0.7 K */
    CHECK_EQ(sh_heater_rise(120, 1000), 9);
    /* around the floor of 0.5 K: 2 kW with 290 l gives 0.49 K, with 280 l 0.51 K */
    CHECK_EQ(sh_heater_rise(20, 290), 5);
    CHECK_EQ(sh_heater_rise(20, 280), 5);
    CHECK_EQ(sh_heater_rise(20, 260), 6);
}

/* The pump stays off (cold collector); the tank goes from `from` by `rise` in 10 min (11 samples,
 * a minute apart). Is the sign there with the threshold `thr`? */
static bool sign_after(int16_t from, int16_t rise, int16_t thr)
{
    sh_pump_ctl_t c = { 0 };
    sh_pump_params_t p = P;
    p.heater_rise = thr;
    bool before = false;
    for (int m = 0; m <= 10; m++) {
        const sh_pump_temps_t t = { .t_col = 100, .t_tank = (int16_t)(from + rise * m / 10) };
        before = before || sh_pump_ctl_heater(&c, thr);     /* not before 10 min are over */
        sh_pump_ctl_step(&c, &p, &t, 1000 + (uint32_t)m * 60);
        CHECK(!c.on);
    }
    CHECK(!before);
    return sh_pump_ctl_heater(&c, thr);
}

static void test_heater_sign(void)
{
    /* "0 kW": 0.5 K */
    const int16_t none = sh_heater_rise(0, 150);
    CHECK(!sign_after(400, 4, none));
    CHECK(sign_after(400, 5, none));
    /* 2 kW, 150 l: 1.0 K — the small movement of the layers does not count */
    const int16_t two = sh_heater_rise(20, 150);
    CHECK(!sign_after(400, 5, two));
    CHECK(!sign_after(400, 9, two));
    CHECK(sign_after(400, 10, two));
    CHECK(sign_after(400, 19, two));
    /* 12 kW, 150 l: 5.7 K */
    const int16_t twelve = sh_heater_rise(120, 150);
    CHECK(!sign_after(400, 56, twelve));
    CHECK(sign_after(400, 57, twelve));
    /* a threshold below 0.5 K is never used */
    CHECK(!sign_after(400, 4, 0));
    CHECK(sign_after(400, 5, 0));
    /* a falling or a steady tank */
    CHECK(!sign_after(400, 0, none));
    CHECK(!sign_after(400, -20, none));

    /* a running pump starts the 10 min over */
    sh_pump_ctl_t c = { 0 };
    sh_pump_temps_t t = { .t_col = 100, .t_tank = 400 };
    uint32_t now = 1000;
    for (int m = 0; m <= 10; m++, now += 60) {
        t.t_tank = (int16_t)(400 + 2 * m);
        sh_pump_ctl_step(&c, &P, &t, now);
    }
    CHECK(sh_pump_ctl_heater(&c, 10));
    t.t_col = 600;                              /* the pump charges */
    sh_pump_ctl_step(&c, &P, &t, now);
    CHECK(c.on);
    sh_pump_ctl_step(&c, &P, &t, now + 1);      /* (the samples follow the pump of the step before) */
    CHECK(!sh_pump_ctl_heater(&c, 10));
}

/* The probes with a faulty collector sensor are postponed by the same threshold. */
static void test_heater_col_fault(void)
{
    for (int pass = 0; pass < 2; pass++) {
        sh_pump_ctl_t c = { 0 };
        sh_pump_params_t p = P;
        p.heater_rise = sh_heater_rise(35, 120);            /* 2.1 K */
        /* 10 min with the pump off (normal mode, cold collector): the tank rises 1.5 K / 2.1 K */
        const int16_t rise = pass == 0 ? 15 : 21;
        uint32_t now = 1000;
        sh_pump_temps_t t = { .t_col = 100, .t_tank = 400 };
        for (int m = 0; m <= 10; m++, now += 60) {
            t.t_tank = (int16_t)(400 + rise * m / 10);
            sh_pump_ctl_step(&c, &p, &t, now);
        }
        t.col_fault = true;
        sh_pump_ctl_step(&c, &p, &t, now - 50);             /* (before the next sample of the tank) */
        if (pass == 0) {
            CHECK(c.on);                                    /* below the threshold: no heater, the probe */
            CHECK_EQ(c.reason, SH_PUMP_REASON_CF_PROBE);
        } else {
            CHECK(!c.on);                                   /* the heater: the probe waits */
            CHECK_EQ(c.reason, SH_PUMP_REASON_CF_HEATER);
        }
    }
}

static void test_heater_found(void)
{
    bool found = false;
    /* a power is set: nothing to find */
    CHECK(!sh_heater_found(&found, 20, true));
    CHECK(!found);
    CHECK(sh_heater_present(20, false));
    /* "0 kW" and no sign: no heater */
    CHECK(!sh_heater_found(&found, 0, false));
    CHECK(!found);
    CHECK(!sh_heater_present(0, false));
    /* "0 kW" and the sign: found (once), taken as present */
    CHECK(sh_heater_found(&found, 0, true));
    CHECK(found);
    CHECK(!sh_heater_found(&found, 0, true));
    CHECK(sh_heater_present(0, found));
    /* it stays found after the sign is gone */
    CHECK(!sh_heater_found(&found, 0, false));
    CHECK(found);
}

/* ---- the heater leak: daylight (3.3) ------------------------------------------------------- */

static void test_daylight(void)
{
    /* nothing learned: 09:00 - 16:00 */
    CHECK(!sh_leak_daylight(HM(8, 59), -1, -1));
    CHECK(sh_leak_daylight(HM(9, 0), -1, -1));
    CHECK(sh_leak_daylight(HM(12, 0), -1, -1));
    CHECK(sh_leak_daylight(HM(15, 59), -1, -1));
    CHECK(!sh_leak_daylight(HM(16, 0), -1, -1));
    CHECK(!sh_leak_daylight(0, -1, -1));
    CHECK(!sh_leak_daylight(1439, -1, -1));
    /* only one of the two learned: still the fixed window */
    CHECK(!sh_leak_daylight(HM(8, 59), HM(5, 30), -1));
    CHECK(sh_leak_daylight(HM(9, 0), HM(5, 30), -1));
    CHECK(sh_leak_daylight(HM(15, 59), -1, HM(20, 30)));
    CHECK(!sh_leak_daylight(HM(16, 0), -1, HM(20, 30)));
    /* learned sunrise 05:42 and sunset 20:17: 06:42 - 19:17 */
    CHECK(!sh_leak_daylight(HM(6, 41), HM(5, 42), HM(20, 17)));
    CHECK(sh_leak_daylight(HM(6, 42), HM(5, 42), HM(20, 17)));
    CHECK(sh_leak_daylight(HM(19, 16), HM(5, 42), HM(20, 17)));
    CHECK(!sh_leak_daylight(HM(19, 17), HM(5, 42), HM(20, 17)));
    /* a winter day, shorter than the fixed window: 08:10 - 15:40 */
    CHECK(!sh_leak_daylight(HM(8, 9), HM(7, 10), HM(16, 40)));
    CHECK(sh_leak_daylight(HM(8, 10), HM(7, 10), HM(16, 40)));
    CHECK(sh_leak_daylight(HM(15, 39), HM(7, 10), HM(16, 40)));
    CHECK(!sh_leak_daylight(HM(15, 40), HM(7, 10), HM(16, 40)));
    /* learned times that leave no daylight are not used */
    CHECK(sh_leak_daylight(HM(12, 0), HM(11, 0), HM(13, 0)));
    CHECK(!sh_leak_daylight(HM(8, 59), HM(11, 0), HM(13, 0)));
}

/* ---- the heater leak: schedule of the checks (3.3) ----------------------------------------- */

/* The leak logic alone: the pump charges without a break from `start` (local minutes) with a
 * reliable clock; returns after how many seconds the first check starts, 0 if none in `limit_s`. */
static uint32_t first_check(uint16_t start, int rise, int set, uint32_t limit_s)
{
    sh_leak_t l = { 0 };
    for (uint32_t s = 0; s <= limit_s; s += 10) {
        const sh_leak_in_t in = {
            .heater = true, .reliable = true, .min = (uint16_t)((start + s / 60) % 1440), .rise = (int16_t)rise,
            .set = (int16_t)set, .pump_on = true, .charging = true, .t_col = 600,
        };
        sh_leak_step(&l, &in, 5000 + s);
        if (l.events & SH_LEAK_EVT_CHECK) {
            CHECK(l.hold);
            CHECK_EQ(l.state, SH_LEAK_CHECK);
            return s;
        }
        CHECK(!l.hold && !l.block && !l.resume);
    }
    return 0;
}

static void test_schedule_clock(void)
{
    /* out of the daylight: after 60 min of charging without a break */
    CHECK_EQ(first_check(HM(17, 0), -1, -1, 7200), 3600);
    CHECK_EQ(first_check(HM(6, 0), -1, -1, 7200), 3600);
    CHECK_EQ(first_check(HM(23, 30), -1, -1, 7200), 3600);          /* over midnight */
    /* in the daylight: none */
    CHECK_EQ(first_check(HM(9, 0), -1, -1, 6 * 3600), 0);
    CHECK_EQ(first_check(HM(6, 42), HM(5, 42), HM(20, 17), 12 * 3600), 0);
    /* the time counts from the end of the daylight: 15:00 -> 16:00 + 60 min */
    CHECK_EQ(first_check(HM(15, 0), -1, -1, 4 * 3600), 2 * 3600);
    /* a pump that starts before the daylight and reaches it is not checked there */
    CHECK_EQ(first_check(HM(8, 30), -1, -1, 3 * 3600), 0);

    /* the evening — the last 2 h before the learned sunset (20:17), of which the hour after the
     * daylight (19:17) is checked: after 30 min */
    CHECK_EQ(first_check(HM(19, 17), HM(5, 42), HM(20, 17), 7200), 1800);
    CHECK_EQ(first_check(HM(18, 0), HM(5, 42), HM(20, 17), 4 * 3600), 77 * 60 + 1800);
    CHECK_EQ(first_check(HM(20, 16), HM(5, 42), HM(20, 17), 7200), 1800);   /* started in the evening */
    /* started at the sunset or later: 60 min */
    CHECK_EQ(first_check(HM(20, 17), HM(5, 42), HM(20, 17), 7200), 3600);
    /* the pump charges from 12:00 through the end of the daylight (the clarification of the task):
     * fixed window, nothing learned -> the first check at 17:00; learned sunset 17:00 (daylight
     * until 16:00) -> at 16:30 */
    CHECK_EQ(first_check(HM(12, 0), -1, -1, 8 * 3600), 5 * 3600);
    CHECK_EQ(first_check(HM(12, 0), HM(6, 0), HM(17, 0), 8 * 3600), 4 * 3600 + 1800);
    /* the morning before the learned daylight: 60 min */
    CHECK_EQ(first_check(HM(4, 0), HM(5, 42), HM(20, 17), 7200), 3600);
    /* no learned sunset: no evening */
    CHECK_EQ(first_check(HM(16, 0), HM(5, 42), -1, 7200), 3600);

    /* a break starts the time over; only charging by the difference counts */
    sh_leak_t l = { 0 };
    sh_leak_in_t in = { .heater = true, .reliable = true, .min = HM(22, 0), .rise = -1, .set = -1,
                        .pump_on = true, .charging = true, .t_col = 600 };
    uint32_t now = 5000;
    for (; now < 5000 + 3590; now += 10) {
        sh_leak_step(&l, &in, now);
        CHECK_EQ(l.events, 0);
    }
    in.charging = in.pump_on = false;                   /* the pump stopped by the difference */
    sh_leak_step(&l, &in, now);
    now += 10;
    in.charging = in.pump_on = true;
    const uint32_t again = now;
    for (; now < again + 3600; now += 10) {
        sh_leak_step(&l, &in, now);
        CHECK_EQ(l.events, 0);
    }
    sh_leak_step(&l, &in, now);
    CHECK_EQ(l.events, SH_LEAK_EVT_CHECK);

    /* the pump runs for another reason (protection, cooling, test, air venting, probes): no check */
    memset(&l, 0, sizeof(l));
    in.charging = false;
    for (now = 5000; now < 5000 + 4 * 3600; now += 10) {
        sh_leak_step(&l, &in, now);
        CHECK_EQ(l.events, 0);
        CHECK(!l.hold);
    }
    /* no heater: no check */
    memset(&l, 0, sizeof(l));
    in.charging = true;
    in.heater = false;
    for (now = 5000; now < 5000 + 4 * 3600; now += 10) {
        sh_leak_step(&l, &in, now);
        CHECK_EQ(l.events, 0);
        CHECK(!l.hold);
    }
}

/* Without a reliable clock: the collector changes by `per_min` every minute while the pump
 * charges; returns after how many seconds the first check starts, 0 if none in `limit_s`. */
static uint32_t first_check_no_clock(int per_min, int bump_at_min, uint32_t limit_s)
{
    sh_leak_t l = { 0 };
    int col = 900;
    for (uint32_t s = 0; s <= limit_s; s += 10) {
        if (s && s % 60 == 0) {
            col += (int)(s / 60) == bump_at_min ? 1 : per_min;
        }
        /* the local time (not reliable) is in the "daylight": it does not matter */
        const sh_leak_in_t in = { .heater = true, .reliable = false, .min = HM(12, 0), .rise = -1, .set = -1,
                                  .pump_on = true, .charging = true, .t_col = (int16_t)col };
        sh_leak_step(&l, &in, 5000 + s);
        if (l.events & SH_LEAK_EVT_CHECK) {
            return s;
        }
    }
    return 0;
}

static void test_schedule_no_clock(void)
{
    /* the collector falls for 30 min in a row: a check */
    CHECK_EQ(first_check_no_clock(-1, -1, 7200), 1800);
    CHECK_EQ(first_check_no_clock(-10, -1, 7200), 1800);
    /* steady or rising: no check, however long the pump runs */
    CHECK_EQ(first_check_no_clock(0, -1, 4 * 3600), 0);
    CHECK_EQ(first_check_no_clock(1, -1, 4 * 3600), 0);
    /* a rise at the 20th minute starts the 30 min over */
    CHECK_EQ(first_check_no_clock(-1, 20, 7200), 20 * 60 + 1800);
    /* a rise at the 30th minute (the collector is still lower than at the start) too */
    CHECK_EQ(first_check_no_clock(-1, 30, 7200), 30 * 60 + 1800);
}

/* ---- the heater leak: the check, its result, the new start (3.3) --------------------------- */

/* The control as sh_control runs it: sunrise / sunset, the leak logic, the pump. */
typedef struct {
    sh_pump_ctl_t c;
    sh_leak_t l;
    sh_sun_t sun;
    sh_pump_params_t p;
    sh_pump_temps_t t;
    bool heater, reliable;
    uint32_t now;
    uint32_t clock_s;           /* local time of the day, s */
    uint8_t events;             /* SH_LEAK_EVT_* since rig_clear() */
} rig_t;

static rig_t rig(uint16_t min, int16_t col, int16_t tank)
{
    rig_t r;
    memset(&r, 0, sizeof(r));
    sh_sun_days_reset(&r.sun.days);
    r.p = P;
    r.t.t_col = col;
    r.t.t_tank = tank;
    r.heater = true;
    r.reliable = true;
    r.now = 10000;
    r.clock_s = (uint32_t)min * 60;
    return r;
}

static void rig_step(rig_t *r)
{
    const sh_local_t lt = { .set = r->reliable, .day = 2500, .min = (uint16_t)(r->clock_s / 60 % 1440) };
    const int16_t col = r->t.col_fault ? SH_TEMP_INVALID : r->t.t_col;
    const int16_t tank = r->t.tank_fault ? SH_TEMP_INVALID : r->t.t_tank;
    const uint8_t sun = sh_sun_step(&r->sun, col, tank, r->c.on, &lt, r->reliable, r->now);
    const sh_leak_in_t in = {
        .heater = r->heater, .reliable = r->reliable, .min = lt.min,
        .rise = (int16_t)sh_sun_avg(r->sun.days.rise), .set = (int16_t)sh_sun_avg(r->sun.days.set),
        .pump_on = r->c.on,
        .charging = r->c.on && r->c.mode == SH_PUMP_MODE_NORMAL &&
                    (r->c.reason == SH_PUMP_REASON_DIFF || r->c.reason == SH_PUMP_REASON_MIN_RUN),
        .t_col = col, .warming = r->sun.warming, .sunrise = (sun & SH_SUN_EVT_RISE) != 0,
    };
    sh_leak_step(&r->l, &in, r->now);
    r->events |= r->l.events;
    r->p.leak_hold = r->l.hold;
    r->p.leak_block = r->l.block;
    r->p.leak_resume = r->l.resume;
    sh_pump_ctl_step(&r->c, &r->p, &r->t, r->now);
    r->c.events = 0;
}

/* Steps of 10 s for `s` seconds. */
static void rig_run(rig_t *r, uint32_t s)
{
    for (uint32_t i = 0; i < s; i += 10) {
        r->now += 10;
        r->clock_s += 10;
        rig_step(r);
    }
}

/* Runs until the pump changes (at most `limit_s`); returns the seconds it took. */
static uint32_t rig_until_switch(rig_t *r, uint32_t limit_s)
{
    const bool on = r->c.on;
    for (uint32_t s = 10; s <= limit_s; s += 10) {
        rig_run(r, 10);
        if (r->c.on != on) {
            return s;
        }
    }
    return 0;
}

/* The pump charges in the evening and is stopped for the check after an hour. */
static rig_t rig_in_check(int16_t col, int16_t tank)
{
    rig_t r = rig(HM(18, 0), col, tank);
    rig_step(&r);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_DIFF);
    const uint32_t ran = rig_until_switch(&r, 2 * 3600);
    CHECK(ran >= 3600 && ran <= 3620);
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_CHECK);
    CHECK_EQ(r.events, SH_LEAK_EVT_CHECK);
    r.events = 0;
    return r;
}

static void test_check_result(void)
{
    /* the collector holds: the pump goes on at once after the 5 min */
    rig_t r = rig_in_check(600, 500);
    uint32_t off = rig_until_switch(&r, 3600);
    CHECK(off >= 300 && off <= 310);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_DIFF);
    CHECK_EQ(r.events, SH_LEAK_EVT_SUN);
    CHECK_EQ(r.l.last, SH_LEAK_RES_SUN);
    CHECK(!r.l.info);
    /* and is checked again an hour later */
    r.events = 0;
    const uint32_t ran = rig_until_switch(&r, 2 * 3600);
    CHECK(ran >= 3600 && ran <= 3620);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_CHECK);

    /* the collector rises during the check (sun) */
    r = rig_in_check(600, 500);
    rig_run(&r, 150);
    r.t.t_col = 650;
    off = rig_until_switch(&r, 3600);
    CHECK(off >= 150 && off <= 160);
    CHECK(r.c.on);

    /* it falls 1.9 K in the 5 min: no leak */
    r = rig_in_check(600, 500);
    rig_run(&r, 290);
    CHECK(!r.c.on);                                     /* the whole 5 min */
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_CHECK);
    r.t.t_col = 581;
    rig_run(&r, 20);
    CHECK(r.c.on);
    CHECK_EQ(r.events, SH_LEAK_EVT_SUN);
    CHECK_EQ(r.l.drop, 19);

    /* it falls 2.0 K: a leak — the pump stays off although the difference is enough */
    r = rig_in_check(600, 500);
    rig_run(&r, 290);
    r.t.t_col = 580;
    rig_run(&r, 20);
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_STOP);
    CHECK_EQ(r.events, SH_LEAK_EVT_LEAK);
    CHECK_EQ(r.l.drop, 20);
    CHECK_EQ(r.l.last, SH_LEAK_RES_LEAK);
    CHECK_EQ(r.l.state, SH_LEAK_STOPPED);
    CHECK(r.l.info);                                    /* "the heater leak is stopped" */
    rig_run(&r, 3 * 3600);
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_STOP);
    CHECK(r.l.info);

    /* a fall early in the check that is made up by its end: no leak */
    r = rig_in_check(600, 500);
    rig_run(&r, 100);
    r.t.t_col = 570;
    rig_run(&r, 100);
    CHECK(!r.c.on);
    r.t.t_col = 590;
    rig_run(&r, 120);
    CHECK(r.c.on);
    CHECK_EQ(r.events, SH_LEAK_EVT_SUN);
}

/* The minimum times do not delay the check or the new start; the pump goes on as a running pump. */
static void test_check_minimum_times(void)
{
    /* minimum off time 10 min: the pump is back after the 5 min of the check */
    rig_t r = rig(HM(18, 0), 600, 500);
    r.p.min_off_s = 600;
    rig_step(&r);
    CHECK(r.c.on);
    CHECK(rig_until_switch(&r, 2 * 3600) > 0);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_CHECK);
    const uint32_t off = rig_until_switch(&r, 3600);
    CHECK(off >= 300 && off <= 310);
    CHECK(r.c.on);
    /* its minimum run time counts from the new start */
    r.t.t_col = 530;                                    /* difference 3 K: below the switch-off value */
    rig_run(&r, 100);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_MIN_RUN);
    rig_run(&r, 100);
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_DIFF);

    /* a difference between the two values (5 K): a stopped pump would not start, the checked one
     * goes on — it was running */
    r = rig(HM(18, 0), 600, 500);
    rig_step(&r);
    r.t.t_col = 550;
    CHECK(rig_until_switch(&r, 2 * 3600) > 0);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_CHECK);
    CHECK(rig_until_switch(&r, 3600) <= 310);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_DIFF);

    /* the difference at the switch-off value after the check: the pump stays off, by the difference */
    r = rig(HM(18, 0), 600, 500);
    rig_step(&r);
    CHECK(rig_until_switch(&r, 2 * 3600) > 0);
    r.t.t_col = 585;                                    /* (not a leak: 1.5 K) */
    r.t.t_tank = 545;
    rig_run(&r, 320);
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_DIFF);
    CHECK_EQ(r.l.state, SH_LEAK_IDLE);
    CHECK(!r.l.info);

    /* the check does not wait for the minimum run time either (10 min after a new start) */
    sh_pump_params_t p = P;
    sh_pump_state_t st = { .on = true, .reason = SH_PUMP_REASON_DIFF };
    p.leak_hold = true;
    sh_pump_decide(&p, &st, 600, 500, 10);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_LEAK_CHECK);
}

/* After a leak: no start by the difference until the collector warms up by itself, the next
 * sunrise, or the daylight. */
static rig_t rig_stopped(uint16_t min)
{
    rig_t r = rig(min, 600, 500);
    rig_step(&r);
    CHECK(rig_until_switch(&r, 2 * 3600) > 0);
    rig_run(&r, 290);
    r.t.t_col = 570;
    rig_run(&r, 20);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_STOP);
    CHECK(r.l.block);
    r.events = 0;
    return r;
}

static void test_new_start(void)
{
    /* the collector warms up by itself: 1.9 K in 10 min is not enough, 2 K is */
    rig_t r = rig_stopped(HM(18, 0));
    rig_run(&r, 1200);                                  /* (at 57.0 °C, the difference is 7 K) */
    for (int m = 1; m <= 10; m++) {
        r.t.t_col = (int16_t)(570 + 19 * m / 10);
        rig_run(&r, 60);
    }
    rig_run(&r, 600);
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_STOP);
    CHECK_EQ(r.events, 0);
    for (int m = 1; m <= 10; m++) {
        r.t.t_col = (int16_t)(589 + 2 * m);
        rig_run(&r, 60);
    }
    CHECK_EQ(r.events, SH_LEAK_EVT_RELEASED);
    CHECK(r.c.on);                                      /* at once: the difference is 10.9 K */
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_DIFF);
    CHECK_EQ(r.l.state, SH_LEAK_IDLE);
    rig_run(&r, 10);
    CHECK(!r.l.info);                                   /* the information ends with the new start */

    /* released with a small difference: the information stays until the pump starts again */
    r = rig_stopped(HM(18, 0));
    r.t.t_col = 500;
    r.t.t_tank = 500;
    rig_run(&r, 1200);
    for (int m = 1; m <= 10; m++) {
        r.t.t_col = (int16_t)(500 + 2 * m);
        rig_run(&r, 60);
    }
    CHECK_EQ(r.events, SH_LEAK_EVT_RELEASED);
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_DIFF);          /* 2 K: not enough yet */
    CHECK(r.l.info);
    r.t.t_col = 570;
    rig_run(&r, 20);
    CHECK(r.c.on);
    rig_run(&r, 10);
    CHECK(!r.l.info);

    /* the daylight: stopped in the early morning, released at 09:00 */
    r = rig_stopped(HM(6, 0));
    rig_run(&r, ((HM(8, 59) - HM(7, 6)) * 60u));
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_STOP);
    rig_run(&r, 120);
    CHECK(r.events & SH_LEAK_EVT_RELEASED);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_DIFF);

    /* the next sunrise */
    sh_leak_t l = { .state = SH_LEAK_STOPPED, .block = true, .info = true };
    sh_leak_in_t in = { .heater = true, .reliable = false, .rise = -1, .set = -1, .t_col = 300 };
    sh_leak_step(&l, &in, 100);
    CHECK(l.block);
    CHECK_EQ(l.events, 0);
    in.sunrise = true;
    sh_leak_step(&l, &in, 110);
    CHECK(!l.block);
    CHECK_EQ(l.events, SH_LEAK_EVT_RELEASED);
    /* without a reliable clock the local time does not release */
    l = (sh_leak_t){ .state = SH_LEAK_STOPPED, .block = true, .info = true };
    in.sunrise = false;
    in.min = HM(12, 0);
    sh_leak_step(&l, &in, 120);
    CHECK(l.block);
    in.reliable = true;
    sh_leak_step(&l, &in, 130);
    CHECK(!l.block);

    /* the heater is taken out of the installer data: nothing stays stopped */
    r = rig_stopped(HM(18, 0));
    r.heater = false;
    rig_run(&r, 10);
    CHECK(r.c.on);
    CHECK_EQ(r.l.state, SH_LEAK_IDLE);
    CHECK(!r.l.info);
}

/* The priorities: the protections act during a check and after a leak; other modes are not
 * checked and end a check. */
static void test_leak_priority(void)
{
    sh_pump_params_t p = P;
    sh_pump_state_t st = { 0 };

    /* a check / a leak only take the place of the difference */
    p.leak_hold = true;
    sh_pump_decide(&p, &st, 600, 500, 1000);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_LEAK_CHECK);
    p.leak_hold = false;
    p.leak_block = true;
    sh_pump_decide(&p, &st, 600, 500, 1000);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_LEAK_STOP);
    sh_pump_decide(&p, &st, 500, 500, 1000);            /* also when the difference is small */
    CHECK_EQ(st.reason, SH_PUMP_REASON_LEAK_STOP);

    for (int hold = 0; hold < 2; hold++) {
        p = P;
        p.leak_hold = hold == 1;
        p.leak_block = hold == 0;
        /* collector protection */
        st = (sh_pump_state_t){ 0 };
        sh_pump_decide(&p, &st, 1100, 500, 0);
        CHECK(st.on);
        CHECK_EQ(st.reason, SH_PUMP_REASON_COL_PROTECT);
        /* frost protection */
        st = (sh_pump_state_t){ 0 };
        sh_pump_decide(&p, &st, 30, 300, 0);
        CHECK(st.on);
        CHECK_EQ(st.reason, SH_PUMP_REASON_FROST);
        /* night cooling */
        st = (sh_pump_state_t){ 0 };
        p.cool = true;
        sh_pump_decide(&p, &st, 300, 720, 1000);
        CHECK(st.on);
        CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);
        p.cool = false;
        /* the limits that stop the pump keep their reasons */
        st = (sh_pump_state_t){ 0 };
        sh_pump_decide(&p, &st, 900, 700, 1000);
        CHECK(!st.on);
        CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_MAX);
        st = (sh_pump_state_t){ 0 };
        sh_pump_decide(&p, &st, 1280, 500, 1000);
        CHECK_EQ(st.reason, SH_PUMP_REASON_COL_BOILING);
        st = (sh_pump_state_t){ 0 };
        p.charge_off = true;
        sh_pump_decide(&p, &st, 600, 500, 1000);
        CHECK_EQ(st.reason, SH_PUMP_REASON_VACATION);
    }

    /* the collector protection starts the pump during a check: the check ends without a result,
     * and the protection is not checked however long it runs */
    rig_t r = rig_in_check(600, 500);
    rig_run(&r, 100);
    r.t.t_col = 1100;
    rig_run(&r, 20);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_COL_PROTECT);
    CHECK_EQ(r.events, SH_LEAK_EVT_ABORT);
    CHECK_EQ(r.l.state, SH_LEAK_IDLE);
    r.events = 0;
    r.t.t_tank = 600;
    rig_run(&r, 3 * 3600);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_COL_PROTECT);
    CHECK_EQ(r.events, 0);

    /* the pump test during a check and after a leak: it runs, the check ends */
    r = rig_in_check(600, 500);
    sh_pump_ctl_plus(&r.c, 300, r.now);
    rig_run(&r, 20);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_TEST);
    CHECK_EQ(r.events, SH_LEAK_EVT_ABORT);
    r = rig_stopped(HM(18, 0));
    sh_pump_ctl_plus(&r.c, 300, r.now);
    rig_run(&r, 20);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_TEST);
    CHECK(r.l.block);                                   /* the leak stays stopped */
    rig_run(&r, 400);
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_STOP);
    CHECK(r.l.info);

    /* the test and the air venting are not checked */
    r = rig(HM(18, 0), 600, 500);
    sh_pump_ctl_plus(&r.c, 900, r.now);
    rig_step(&r);
    for (int i = 0; i < 20; i++) {
        for (int k = 0; k < 5; k++) {
            sh_pump_ctl_plus(&r.c, 900, r.now);         /* +60 s each: kept running for more than an hour */
        }
        rig_run(&r, 300);
        CHECK(r.c.on);
        CHECK_EQ(r.c.reason, SH_PUMP_REASON_TEST);
    }
    CHECK_EQ(r.events, 0);
    r = rig(HM(18, 0), 300, 500);
    sh_pump_ctl_air_start(&r.c, 60, r.now);
    rig_run(&r, 3 * 360 - 10);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_AIR_PAUSE);
    CHECK_EQ(r.events, 0);

    /* the night cooling is not checked */
    r = rig(HM(4, 0), 300, 720);
    r.p.cool = true;
    rig_run(&r, 3 * 3600);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_COOL);
    CHECK_EQ(r.events, 0);

    /* the frost protection is not checked */
    r = rig(HM(4, 0), 30, 300);
    rig_run(&r, 10);
    CHECK(r.c.on);
    r.t.t_col = 50;                                     /* stays below the threshold + 2 K */
    rig_run(&r, 3 * 3600);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_FROST);
    CHECK_EQ(r.events, 0);

    /* a faulty sensor: its mode is not checked, and a check ends */
    r = rig(HM(18, 0), 700, 500);
    r.t.tank_fault = true;
    rig_run(&r, 3 * 3600);
    CHECK(r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_TF_RUN);
    CHECK_EQ(r.events, 0);
    r = rig_in_check(600, 500);
    r.t.col_fault = true;
    rig_run(&r, 10);
    CHECK_EQ(r.events, SH_LEAK_EVT_ABORT);
    CHECK_EQ(r.l.state, SH_LEAK_IDLE);
    CHECK(r.c.reason != SH_PUMP_REASON_LEAK_CHECK);

    /* no heater: the pump charges all night without a check */
    r = rig(HM(18, 0), 600, 500);
    r.heater = false;
    rig_run(&r, 6 * 3600);
    CHECK(r.c.on);
    CHECK_EQ(r.events, 0);

    /* in the daylight: likewise */
    r = rig(HM(9, 0), 600, 500);
    rig_run(&r, 6 * 3600);
    CHECK(r.c.on);
    CHECK_EQ(r.events, 0);
    CHECK(strcmp(sh_pump_reason_name(SH_PUMP_REASON_LEAK_CHECK), "heater leak check") == 0);
    CHECK(strcmp(sh_pump_reason_name(SH_PUMP_REASON_LEAK_STOP), "heater leak stopped") == 0);
}

/* Without a reliable clock, through the pump: the collector falls while the pump charges. */
static void test_leak_no_clock(void)
{
    rig_t r = rig(HM(12, 0), 900, 500);
    r.reliable = false;
    rig_step(&r);
    CHECK(r.c.on);
    uint32_t ran = 0;
    while (r.c.on && ran < 7200) {
        rig_run(&r, 60);
        ran += 60;
        r.t.t_col = (int16_t)(r.t.t_col - 2);           /* 0.2 K a minute */
    }
    CHECK(ran >= 1800 && ran <= 1920);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_CHECK);
    /* it goes on falling with the pump stopped: 2 K a minute */
    for (int m = 0; m < 6; m++) {
        r.t.t_col = (int16_t)(r.t.t_col - 20);
        rig_run(&r, 60);
    }
    CHECK(!r.c.on);
    CHECK_EQ(r.c.reason, SH_PUMP_REASON_LEAK_STOP);
    CHECK(r.events & SH_LEAK_EVT_LEAK);
}

/* ---- E8: reverse thermosiphon (3.4) -------------------------------------------------------- */

static void test_e8_fall(void)
{
    /* half of loss x area x 40 K / (volume x 1.163), 0.1 K/h */
    CHECK_EQ(sh_e8_fall(false, 18, 120), 10);       /* the example: 1.8 m² flat, 120 l -> ~1 K/h */
    CHECK_EQ(sh_e8_fall(true, 18, 120), 4);         /* evacuated tubes: 1.5 instead of 4 W/m²K */
    CHECK_EQ(sh_e8_fall(false, 20, 150), 9);        /* the defaults */
    CHECK_EQ(sh_e8_fall(false, 200, 50), 275);      /* the highest */
    CHECK_EQ(sh_e8_fall(true, 5, 1000), 1);         /* the lowest: at least 0.1 K/h */
    CHECK_EQ(sh_e8_fall(false, 5, 1000), 1);
}

typedef struct {
    sh_e8_t e;
    uint32_t now;
} e8_rig_t;

static e8_rig_t e8_rig(void)
{
    e8_rig_t r;
    memset(&r, 0, sizeof(r));
    sh_e8_keep_reset(&r.e.keep);
    r.now = 50000;
    return r;
}

/* What a night is made of: the collector and the tank change evenly, tenths of K per hour. */
typedef struct {
    int minutes;
    int16_t col, tank;
    int col_h, tank_h;
    int pump_from, pump_to;     /* the pump runs in these minutes (from == to: never) */
    int heater_to;              /* the heater sign is active in the minutes before this one */
    bool col_bad;               /* the collector sensor is faulty */
} night_t;

#define NIGHT(...)  ((night_t){ __VA_ARGS__ })       /* 0: 60 min, collector 20 °C, tank 60 °C */
#define SIGN        NIGHT(.col_h = 20, .tank_h = -10)       /* the sign with the threshold 1.0 K/h */
#define QUIET       NIGHT(.col_h = -5, .tank_h = -4)        /* a normal night */

/* One night and the first step of the day after it; returns the events. */
static uint8_t e8_night(e8_rig_t *r, night_t n)
{
    uint8_t events = 0;
    sh_e8_in_t in = { .night = true, .fall = 10 };
    n.minutes = n.minutes ? n.minutes : 60;
    n.col = n.col ? n.col : 200;
    n.tank = n.tank ? n.tank : 600;
    for (int m = 0; m <= n.minutes; m++, r->now += 60) {
        in.pump_on = m >= n.pump_from && m < n.pump_to;
        in.t_col = n.col_bad ? SH_TEMP_INVALID : (int16_t)(n.col + n.col_h * m / 60);
        in.t_tank = (int16_t)(n.tank + n.tank_h * m / 60);
        in.heater = m < n.heater_to;
        const bool e8 = sh_e8_step(&r->e, &in, r->now);
        CHECK_EQ(e8, r->e.keep.active);
        events |= r->e.events;
    }
    in.night = false;
    sh_e8_step(&r->e, &in, r->now);
    events |= r->e.events;
    r->now += 12 * 3600;
    return events;
}

/* Every condition of the sign, each one alone at its limit. */
static void test_e8_sign(void)
{
    e8_rig_t r = e8_rig();
    CHECK_EQ(e8_night(&r, SIGN), SH_E8_EVT_SIGN | SH_E8_EVT_STORE);
    CHECK_EQ(r.e.keep.sign_nights, 1);

    /* the collector rises 1.9 K in the hour: no sign (a normal night) */
    r = e8_rig();
    CHECK_EQ(e8_night(&r, NIGHT(.col_h = 19, .tank_h = -10)), SH_E8_EVT_NORMAL | SH_E8_EVT_STORE);
    CHECK_EQ(r.e.keep.sign_nights, 0);
    /* ... 2.0 K is the sign (above); a falling collector is the usual night */
    r = e8_rig();
    CHECK(!(e8_night(&r, NIGHT(.col_h = -30, .tank_h = -40)) & SH_E8_EVT_SIGN));

    /* the tank falls 0.9 K/h, below half of the thermosiphon rate (1.0 K/h): no sign */
    r = e8_rig();
    CHECK(!(e8_night(&r, NIGHT(.col_h = 20, .tank_h = -9)) & SH_E8_EVT_SIGN));
    r = e8_rig();
    CHECK(e8_night(&r, NIGHT(.col_h = 20, .tank_h = -10)) & SH_E8_EVT_SIGN);
    /* a rising tank (the heater) */
    r = e8_rig();
    CHECK(!(e8_night(&r, NIGHT(.col_h = 40, .tank_h = 30)) & SH_E8_EVT_SIGN));

    /* the learned normal cooling is 1.5 K/h: the tank must fall faster than that */
    r = e8_rig();
    r.e.keep.rate[SH_E8_LEARN_NIGHTS - 1] = 15;
    CHECK_EQ(sh_e8_normal(&r.e.keep), 15);
    CHECK(!(e8_night(&r, NIGHT(.col_h = 20, .tank_h = -10)) & SH_E8_EVT_SIGN));
    r = e8_rig();
    r.e.keep.rate[SH_E8_LEARN_NIGHTS - 1] = 15;
    CHECK(!(e8_night(&r, NIGHT(.col_h = 20, .tank_h = -15)) & SH_E8_EVT_SIGN));
    r = e8_rig();
    r.e.keep.rate[SH_E8_LEARN_NIGHTS - 1] = 15;
    CHECK(e8_night(&r, NIGHT(.col_h = 20, .tank_h = -16)) & SH_E8_EVT_SIGN);
    /* a learned cooling below the thermosiphon threshold: the threshold decides */
    r = e8_rig();
    r.e.keep.rate[SH_E8_LEARN_NIGHTS - 1] = 3;
    CHECK(!(e8_night(&r, NIGHT(.col_h = 20, .tank_h = -9)) & SH_E8_EVT_SIGN));
    r = e8_rig();
    r.e.keep.rate[SH_E8_LEARN_NIGHTS - 1] = 3;
    CHECK(e8_night(&r, NIGHT(.col_h = 20, .tank_h = -10)) & SH_E8_EVT_SIGN);

    /* the collector and the tank must come closer: a collector above the tank moves away */
    r = e8_rig();
    CHECK(!(e8_night(&r, NIGHT(.col = 700, .col_h = 20, .tank_h = -10)) & SH_E8_EVT_SIGN));
    /* (just below the tank, crossing it within the hour: closer) */
    r = e8_rig();
    CHECK(e8_night(&r, NIGHT(.col = 580, .col_h = 20, .tank_h = -10)) & SH_E8_EVT_SIGN);

    /* the pump stopped for the whole hour: 59 min are not enough ... */
    r = e8_rig();
    CHECK_EQ(e8_night(&r, NIGHT(.minutes = 59, .col_h = 20, .tank_h = -10)), SH_E8_EVT_SKIPPED);
    /* ... and a run of the pump starts the hour over */
    r = e8_rig();
    CHECK_EQ(e8_night(&r, NIGHT(.minutes = 90, .col_h = 20, .tank_h = -10, .pump_from = 40, .pump_to = 41)),
             SH_E8_EVT_SKIPPED);
    r = e8_rig();
    CHECK(e8_night(&r, NIGHT(.minutes = 101, .col_h = 20, .tank_h = -10, .pump_from = 40, .pump_to = 41)) &
          SH_E8_EVT_SIGN);
    /* a faulty sensor: nothing is judged */
    r = e8_rig();
    CHECK_EQ(e8_night(&r, NIGHT(.minutes = 300, .col_h = 20, .tank_h = -10, .col_bad = true)), SH_E8_EVT_SKIPPED);

    /* by day the same temperatures mean nothing (the sun) */
    r = e8_rig();
    sh_e8_in_t in = { .night = false, .fall = 10 };
    for (int m = 0; m <= 180; m++, r.now += 60) {
        in.t_col = (int16_t)(200 + 20 * m / 60);
        in.t_tank = (int16_t)(600 - 10 * m / 60);
        CHECK(!sh_e8_step(&r.e, &in, r.now));
        CHECK_EQ(r.e.events, 0);
    }
    CHECK_EQ(r.e.keep.sign_nights, 0);

    /* the sign in a later hour of a long night, once a night */
    r = e8_rig();
    sh_e8_in_t n = { .night = true, .fall = 10 };
    int signs = 0;
    for (int m = 0; m <= 600; m++, r.now += 60) {
        const int late = m > 240 ? m - 240 : 0;         /* the valve starts leaking after 4 h */
        n.t_col = (int16_t)(200 - 5 * (m - late) / 60 + 30 * late / 60);
        n.t_tank = (int16_t)(600 - 4 * (m - late) / 60 - 20 * late / 60);
        sh_e8_step(&r.e, &n, r.now);
        signs += (r.e.events & SH_E8_EVT_SIGN) != 0;
    }
    CHECK_EQ(signs, 1);
    CHECK_EQ(r.e.keep.sign_nights, 1);
}

/* 3 nights in a row with the sign declare E8, 3 normal nights in a row clear it. */
static void test_e8_nights(void)
{
    e8_rig_t r = e8_rig();
    CHECK(!r.e.keep.active);
    CHECK_EQ(e8_night(&r, SIGN), SH_E8_EVT_SIGN | SH_E8_EVT_STORE);
    CHECK(!r.e.keep.active);
    CHECK_EQ(e8_night(&r, SIGN), SH_E8_EVT_SIGN | SH_E8_EVT_STORE);
    CHECK(!r.e.keep.active);
    CHECK_EQ(r.e.keep.sign_nights, 2);
    CHECK_EQ(e8_night(&r, SIGN), SH_E8_EVT_SIGN | SH_E8_EVT_STORE | SH_E8_EVT_ON);
    CHECK(r.e.keep.active);
    /* more such nights: it stays */
    CHECK_EQ(e8_night(&r, SIGN), SH_E8_EVT_SIGN | SH_E8_EVT_STORE);
    CHECK(r.e.keep.active);
    CHECK_EQ(r.e.keep.sign_nights, 3);

    /* cleared after 3 normal nights in a row */
    CHECK_EQ(e8_night(&r, QUIET), SH_E8_EVT_NORMAL | SH_E8_EVT_STORE);
    CHECK(r.e.keep.active);
    CHECK_EQ(r.e.keep.sign_nights, 0);
    CHECK_EQ(e8_night(&r, QUIET), SH_E8_EVT_NORMAL | SH_E8_EVT_STORE);
    CHECK(r.e.keep.active);
    CHECK_EQ(r.e.keep.normal_nights, 2);
    CHECK_EQ(e8_night(&r, QUIET), SH_E8_EVT_NORMAL | SH_E8_EVT_STORE | SH_E8_EVT_OFF);
    CHECK(!r.e.keep.active);

    /* a normal night breaks the series of the nights with the sign */
    r = e8_rig();
    e8_night(&r, SIGN);
    e8_night(&r, SIGN);
    e8_night(&r, QUIET);
    e8_night(&r, SIGN);
    CHECK(!r.e.keep.active);
    CHECK_EQ(r.e.keep.sign_nights, 1);
    e8_night(&r, SIGN);
    CHECK(!r.e.keep.active);
    e8_night(&r, SIGN);
    CHECK(r.e.keep.active);
    /* a night with the sign breaks the series of the normal ones */
    e8_night(&r, QUIET);
    e8_night(&r, QUIET);
    e8_night(&r, SIGN);
    CHECK_EQ(r.e.keep.normal_nights, 0);
    e8_night(&r, QUIET);
    e8_night(&r, QUIET);
    CHECK(r.e.keep.active);
    e8_night(&r, QUIET);
    CHECK(!r.e.keep.active);

    /* a night that cannot be judged (the pump ran, a faulty sensor, too short) is not counted and
     * breaks neither series */
    const night_t pumped = NIGHT(.minutes = 300, .col_h = -5, .tank_h = -4, .pump_from = 0, .pump_to = 301);
    const night_t faulty = NIGHT(.minutes = 300, .col_h = 20, .tank_h = -10, .col_bad = true);
    r = e8_rig();
    e8_night(&r, SIGN);
    CHECK_EQ(e8_night(&r, pumped), SH_E8_EVT_SKIPPED);
    e8_night(&r, SIGN);
    CHECK_EQ(e8_night(&r, faulty), SH_E8_EVT_SKIPPED);
    CHECK_EQ(e8_night(&r, NIGHT(.minutes = 30, .col_h = -5, .tank_h = -4)), SH_E8_EVT_SKIPPED);
    CHECK_EQ(r.e.keep.sign_nights, 2);
    e8_night(&r, SIGN);
    CHECK(r.e.keep.active);
    e8_night(&r, QUIET);
    e8_night(&r, pumped);
    e8_night(&r, QUIET);
    e8_night(&r, faulty);
    CHECK(r.e.keep.active);
    CHECK_EQ(r.e.keep.normal_nights, 2);
    e8_night(&r, QUIET);
    CHECK(!r.e.keep.active);

    /* the counters come back from the storage: the 3rd night after a restart declares E8 */
    r = e8_rig();
    r.e.keep.sign_nights = 2;
    e8_night(&r, SIGN);
    CHECK(r.e.keep.active);
    r = e8_rig();
    r.e.keep.active = true;
    r.e.keep.normal_nights = 2;
    e8_night(&r, QUIET);
    CHECK(!r.e.keep.active);
}

/* The learned normal cooling: the last 7 normal nights. */
static void test_e8_learned(void)
{
    e8_rig_t r = e8_rig();
    CHECK_EQ(sh_e8_normal(&r.e.keep), -1);

    /* a night of 8 h in which the tank falls 0.6 K/h */
    e8_night(&r, NIGHT(.minutes = 480, .col_h = -5, .tank_h = -6));
    CHECK_EQ(r.e.keep.rate[SH_E8_LEARN_NIGHTS - 1], 6);
    CHECK_EQ(sh_e8_normal(&r.e.keep), 6);
    /* the average of the nights so far */
    e8_night(&r, NIGHT(.minutes = 480, .col_h = -5, .tank_h = -10));
    CHECK_EQ(sh_e8_normal(&r.e.keep), 8);
    /* a night with the sign and a night that is not counted teach nothing */
    e8_night(&r, SIGN);
    e8_night(&r, NIGHT(.minutes = 30, .col_h = -5, .tank_h = -30));
    CHECK_EQ(sh_e8_normal(&r.e.keep), 8);
    /* the last 7: the oldest (0.6 K/h) is pushed out by seven nights of 1.2 K/h */
    for (int i = 0; i < 6; i++) {
        e8_night(&r, NIGHT(.minutes = 480, .col_h = -5, .tank_h = -12));
    }
    CHECK_EQ(r.e.keep.rate[0], 10);
    CHECK_EQ(sh_e8_normal(&r.e.keep), 12);              /* (10 + 6 x 12) / 7 = 11.7 */
    e8_night(&r, NIGHT(.minutes = 480, .col_h = -5, .tank_h = -12));
    CHECK_EQ(r.e.keep.rate[0], 12);
    CHECK_EQ(sh_e8_normal(&r.e.keep), 12);

    /* without the heater: the tank rises 3 K in the first 2 h (the heater works), then falls
     * 0.5 K/h for 6 h — only the fall is learned */
    r = e8_rig();
    sh_e8_in_t in = { .night = true, .fall = 10 };
    for (int m = 0; m <= 480; m++, r.now += 60) {
        in.heater = m <= 120;
        in.t_col = (int16_t)(200 - 5 * m / 60);
        in.t_tank = (int16_t)(m <= 120 ? 500 + 15 * m / 60 : 530 - 5 * (m - 120) / 60);
        sh_e8_step(&r.e, &in, r.now);
    }
    in.night = false;
    sh_e8_step(&r.e, &in, r.now);
    CHECK_EQ(r.e.events, SH_E8_EVT_NORMAL | SH_E8_EVT_STORE);
    CHECK_EQ(r.e.keep.rate[SH_E8_LEARN_NIGHTS - 1], 5);
    /* a tank that only rises: 0, never negative */
    r = e8_rig();
    e8_night(&r, NIGHT(.minutes = 240, .col_h = -5, .tank_h = 6));
    CHECK_EQ(r.e.keep.rate[SH_E8_LEARN_NIGHTS - 1], 0);

    sh_e8_keep_t k;
    memset(&k, 0x55, sizeof(k));
    sh_e8_keep_reset(&k);
    CHECK(!k.active);
    CHECK_EQ(k.sign_nights, 0);
    CHECK_EQ(k.normal_nights, 0);
    CHECK_EQ(sh_e8_normal(&k), -1);
}

/* E8 as a code: the display after E6, the LED red double blink, the history bit. */
static void test_e8_code(void)
{
    static const sh_alarm_params_t AP = {
        .e6_time_s = 600, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200, .e6_clear_diff = 100,
    };
    sh_alarm_state_t as = { 0 };
    sh_alarm_in_t ain = { .t_col = 300, .t_tank = 500 };
    CHECK_EQ(sh_alarm_step(&as, &AP, &ain, 100), 0);
    ain.e8 = true;
    CHECK_EQ(sh_alarm_step(&as, &AP, &ain, 101), SH_ALARM_BIT(SH_ALARM_E8));
    ain.col_fault = true;                               /* with other codes */
    CHECK_EQ(sh_alarm_step(&as, &AP, &ain, 102), SH_ALARM_BIT(SH_ALARM_E8) | SH_ALARM_BIT(SH_ALARM_E1));
    ain.e8 = false;
    CHECK_EQ(sh_alarm_step(&as, &AP, &ain, 103), SH_ALARM_BIT(SH_ALARM_E1));
    CHECK(strcmp(sh_alarm_name(SH_ALARM_E8), "E8") == 0);

    /* the bits of the codes the stored days already have are not moved */
    CHECK_EQ(SH_ALARM_BIT(SH_ALARM_E1), 0x01);
    CHECK_EQ(SH_ALARM_BIT(SH_ALARM_E6), 0x10);
    CHECK_EQ(SH_ALARM_BIT(SH_ALARM_HOT), 0x20);
    CHECK_EQ(SH_ALARM_BIT(SH_ALARM_FRO), 0x40);
    CHECK_EQ(SH_ALARM_BIT(SH_ALARM_E8), 0x80);

    /* the display: the alarms by number, then the information */
    const uint8_t all = 0xFF;
    static const sh_alarm_t ORDER[] = { SH_ALARM_E1, SH_ALARM_E2, SH_ALARM_E4, SH_ALARM_E5, SH_ALARM_E6,
                                        SH_ALARM_E8, SH_ALARM_HOT, SH_ALARM_FRO };
    for (unsigned i = 0; i < 8; i++) {
        CHECK_EQ(sh_alarm_at((int)i), ORDER[i]);
        CHECK_EQ(sh_alarm_code_at(all, i * SH_ALARM_CODE_MS), ORDER[i]);
    }
    CHECK_EQ(sh_alarm_at(-1), SH_ALARM_COUNT);
    CHECK_EQ(sh_alarm_at(8), SH_ALARM_COUNT);
    CHECK_EQ(sh_alarm_code_at(all, 8 * SH_ALARM_CODE_MS), SH_ALARM_COUNT);
    CHECK_EQ(sh_alarm_count(all), 8);
    const uint8_t some = SH_ALARM_BIT(SH_ALARM_FRO) | SH_ALARM_BIT(SH_ALARM_E8) | SH_ALARM_BIT(SH_ALARM_E2);
    CHECK_EQ(sh_alarm_code_at(some, 0), SH_ALARM_E2);
    CHECK_EQ(sh_alarm_code_at(some, 1000), SH_ALARM_E8);
    CHECK_EQ(sh_alarm_code_at(some, 2000), SH_ALARM_FRO);
    CHECK_EQ(sh_alarm_code_at(some, 3000), SH_ALARM_COUNT);

    /* the LED: red, double blink (a repair); fast with an overheating code */
    static const sh_ind_params_t IP = { .cold = 200, .hot = 600 };
    sh_ind_state_t is = { 0 };
    sh_ind_in_t iin = { .alarms = SH_ALARM_BIT(SH_ALARM_E8), .t_col = 300, .wifi_connected = true };
    sh_ind_out_t out = sh_ind_update(&is, &IP, &iin);
    CHECK_EQ(out.color, SH_COLOR_RED);
    CHECK_EQ(out.pattern, SH_IND_DOUBLE);
    iin.pump_on = true;                                 /* also over the green of the pump */
    iin.vacation = true;
    out = sh_ind_update(&is, &IP, &iin);
    CHECK_EQ(out.color, SH_COLOR_RED);
    CHECK_EQ(out.pattern, SH_IND_DOUBLE);
    iin.alarms |= SH_ALARM_BIT(SH_ALARM_E4);
    out = sh_ind_update(&is, &IP, &iin);
    CHECK_EQ(out.color, SH_COLOR_RED);
    CHECK_EQ(out.pattern, SH_IND_FAST);

    /* the history: the bit in the daily record, the code in the CSV after E6 */
    sh_hist_rec_t rec, back;
    sh_hist_rec_init(&rec, sh_day_number(2026, 11, 12));
    rec.alarms = SH_ALARM_BIT(SH_ALARM_HOT) | SH_ALARM_BIT(SH_ALARM_E8) | SH_ALARM_BIT(SH_ALARM_E6);
    rec.heater_min = 95;
    uint8_t raw[SH_HIST_REC_SIZE];
    sh_hist_encode(&rec, raw);
    CHECK_EQ(sh_hist_decode(raw, &back), SH_HIST_VALID);
    CHECK_EQ(back.alarms, rec.alarms);
    CHECK_EQ(back.heater_min, 95);
    char line[SH_HIST_CSV_LINE_MAX];
    CHECK(sh_hist_csv_line(line, sizeof(line), &rec) > 0);
    CHECK(strcmp(line, "2026-11-12;;;;;0;0;0;0;95;E6 E8 Hot;;0;0;;;\r\n") == 0);
}

/* ---- the heater minutes of the day (3.2) --------------------------------------------------- */

static void test_heater_minutes(void)
{
    sh_day_t d;
    sh_day_init(&d);
    sh_day_sum_t done;
    sh_local_t lt = { .set = true, .day = 2500, .min = 0 };
    sh_day_in_t in = { .t_col = 100, .t_tank = 400 };
    uint32_t now = 1000;
    /* 00:00 - 02:00 nothing, 02:00 - 02:45 the heater sign, then nothing until midnight */
    for (int s = 0; s <= 86400; s += 10, now += 10) {
        lt.min = (uint16_t)(s / 60 % 1440);
        lt.day = (uint16_t)(2500 + s / 86400);
        in.heater = s >= 2 * 3600 && s < 2 * 3600 + 45 * 60;
        if (sh_day_step(&d, &in, &lt, now, &done)) {
            CHECK_EQ(s, 86400);
            CHECK_EQ(done.heater_s, 45 * 60);
            CHECK_EQ(done.run_s[SH_RUN_CHARGE], 0);
        } else if (s == 3 * 3600) {
            CHECK_EQ(d.sum.heater_s, 45 * 60);          /* counted during the day too */
        }
    }
    CHECK_EQ(d.sum.heater_s, 0);                        /* the new day starts at 0 */

    /* without a clock there is no day: nothing is counted */
    sh_day_init(&d);
    lt = (sh_local_t){ .set = false, .day = SH_DAY_NONE };
    in.heater = true;
    for (int s = 0; s < 3600; s += 10, now += 10) {
        CHECK(!sh_day_step(&d, &in, &lt, now, &done));
    }
    CHECK_EQ(d.sum.heater_s, 0);
}

int main(void)
{
    test_installer_heater();
    test_heater_rise();
    test_heater_sign();
    test_heater_col_fault();
    test_heater_found();
    test_daylight();
    test_schedule_clock();
    test_schedule_no_clock();
    test_check_result();
    test_check_minimum_times();
    test_new_start();
    test_leak_priority();
    test_leak_no_clock();
    test_e8_fall();
    test_e8_sign();
    test_e8_nights();
    test_e8_learned();
    test_e8_code();
    test_heater_minutes();
    return test_summary("test_group_d");
}
