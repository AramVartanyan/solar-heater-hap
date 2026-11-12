/* Host test of group D (doc/tasks/step12-heater-leak-e8.md 3.1 - 3.4): the heater power in the
 * installer data (sh_installer); the heater sign and its threshold, the detection with "0 kW", the
 * probes with a faulty collector sensor on the same threshold (sh_heater, sh_pump_ctl); the
 * guaranteed daylight, the schedule of the leak checks, their result, the stop and the new start
 * (sh_heater) and their place in the pump rules (sh_pump_logic); E8 — every condition of the sign,
 * the nights, the learned normal cooling (sh_e8), the code and the indication (sh_alarm, sh_ind);
 * the heater minutes of the day (sh_day, sh_hist).
 * Temperatures are tenths of °C, times seconds; local time in minutes of the day. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_heater.h"
#include "sh_e8.h"
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

/* ---- installer data: the heater power (3.1) ------------------------------------------------ */

static void test_installer(void)
{
    sh_installer_t d = { .v = {
        [SH_INST_FLUID] = SH_FLUID_WATER, [SH_INST_PRESSURE] = 15, [SH_INST_COL_MAX] = 120,
        [SH_INST_COL_TYPE] = SH_COL_FLAT, [SH_INST_COL_AREA] = 20, [SH_INST_TANK_VOL] = 150,
        [SH_INST_HEATER] = 20,
    } };
    CHECK(sh_installer_ok(&d));
    CHECK_EQ(strcmp(sh_inst_info[SH_INST_HEATER].key, "heater"), 0);
    /* 0 = none, 0.5 ... 12.0 kW in steps of 0.5 */
    static const int16_t ok[] = { 0, 5, 10, 35, 115, 120 };
    static const int16_t bad[] = { -5, 1, 4, 7, 21, 125, 130 };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        d.v[SH_INST_HEATER] = ok[i];
        CHECK(sh_installer_ok(&d));
    }
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        d.v[SH_INST_HEATER] = bad[i];
        CHECK(!sh_installer_ok(&d));
    }
    /* out of range from the storage -> the default */
    sh_installer_t def = d;
    def.v[SH_INST_HEATER] = 20;
    d.v[SH_INST_HEATER] = 7;
    CHECK_EQ(sh_installer_sanitize(&d, &def), 1u << SH_INST_HEATER);
    CHECK_EQ(d.v[SH_INST_HEATER], 20);
}

/* ---- the heater sign (3.2) ----------------------------------------------------------------- */

static void test_rise(void)
{
    /* "0 kW": 0.5 K */
    CHECK_EQ(sh_heater_rise(0, 150), 5);
    /* 2.0 kW, 150 l: 11.46 K/h -> 1.91 K in 10 min -> half 0.955 K, rounded up to 1.0 K */
    CHECK_EQ(sh_heater_rise(20, 150), 10);
    /* 12.0 kW, 50 l: 206.4 K/h -> 34.4 K in 10 min -> half 17.2 K */
    CHECK_EQ(sh_heater_rise(120, 50), 172);
    /* 12.0 kW, 1000 l: half 0.86 K -> 0.9 K */
    CHECK_EQ(sh_heater_rise(120, 1000), 9);
    /* the example of the document: 3.5 kW, 120 l -> ~2 K */
    CHECK_EQ(sh_heater_rise(35, 120), 21);
    /* never below 0.5 K: 0.5 kW, 1000 l would be 0.36 K */
    CHECK_EQ(sh_heater_rise(5, 1000), 5);
    /* exactly at 0.5 K: 2.0 kW, 287 l -> 0.4993 K -> 0.5 K; 2.0 kW, 286 l -> 0.501 K -> 0.6 K */
    CHECK_EQ(sh_heater_rise(20, 287), 5);
    CHECK_EQ(sh_heater_rise(20, 286), 6);
}

/* The tank rises from 40.0 °C by `rise` over 10 min with the pump stopped (collector cold, no
 * frost): is the sign on after 10 min? */
static bool sign_after(int16_t heater_rise, int16_t rise, bool col_fault)
{
    sh_pump_ctl_t c;
    memset(&c, 0, sizeof(c));
    sh_pump_params_t p = P;
    p.heater_rise = heater_rise;
    sh_pump_temps_t t = { .t_col = 200, .t_tank = 400, .col_fault = col_fault };
    const uint32_t start = 100000;
    bool on = false;
    for (uint32_t s = start; s <= start + 600; s++) {
        t.t_tank = (int16_t)(400 + (int32_t)rise * (int32_t)(s - start) / 600);
        sh_pump_ctl_step(&c, &p, &t, s);
        CHECK(!c.on || col_fault);
        on = sh_pump_ctl_heater(&c);
        if (s < start + 600) {
            CHECK(!on);                     /* not 10 min yet */
        }
    }
    return on;
}

static void test_sign(void)
{
    /* the threshold of 0 / 2 / 12 kW and the boundary */
    static const struct { int16_t power, volume; } K[] = { { 0, 150 }, { 20, 150 }, { 120, 50 }, { 120, 1000 } };
    for (size_t i = 0; i < sizeof(K) / sizeof(K[0]); i++) {
        const int16_t thr = sh_heater_rise(K[i].power, K[i].volume);
        CHECK(!sign_after(thr, (int16_t)(thr - 1), false));
        CHECK(sign_after(thr, thr, false));
    }
    /* parameters without a threshold (0): 0.5 K, as before */
    CHECK(!sign_after(0, 4, false));
    CHECK(sign_after(0, 5, false));

    /* the probes with a faulty collector sensor wait on the same threshold (2 kW, 150 l: 1.0 K):
     * the probe runs at once when the sensor is declared faulty, so the sign is checked between the
     * probes — a tank rising 0.9 K in 10 min is not the heater */
    sh_pump_ctl_t c;
    memset(&c, 0, sizeof(c));
    sh_pump_params_t p = P;
    p.heater_rise = 10;
    sh_pump_temps_t t = { .t_col = SH_TEMP_INVALID, .t_tank = 400, .col_fault = true };
    uint32_t s = 1000;
    sh_pump_ctl_step(&c, &p, &t, s);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_CF_PROBE);
    for (; s <= 1000 + 600; s++) {
        sh_pump_ctl_step(&c, &p, &t, s);
    }
    CHECK(!c.on);                           /* no rise: waits 30 min */
    CHECK_EQ(c.reason, SH_PUMP_REASON_CF_WAIT);
    const uint32_t next = c.cf_next_s;
    /* the last 10 min before the next probe: the tank rises 0.9 K -> the probe runs */
    for (s = next - 600; s < next; s++) {
        t.t_tank = (int16_t)(400 + 9 * (int32_t)(s - (next - 600)) / 600);
        sh_pump_ctl_step(&c, &p, &t, s);
    }
    t.t_tank = 409;
    sh_pump_ctl_step(&c, &p, &t, next);
    CHECK_EQ(c.reason, SH_PUMP_REASON_CF_PROBE);
    /* the same with 1.0 K: postponed (heater) */
    memset(&c, 0, sizeof(c));
    t.t_tank = 400;
    s = 1000;
    sh_pump_ctl_step(&c, &p, &t, s);
    for (; s <= 1000 + 600; s++) {
        sh_pump_ctl_step(&c, &p, &t, s);
    }
    for (s = next - 600; s < next; s++) {
        t.t_tank = (int16_t)(400 + 10 * (int32_t)(s - (next - 600)) / 600);
        sh_pump_ctl_step(&c, &p, &t, s);
    }
    t.t_tank = 410;
    sh_pump_ctl_step(&c, &p, &t, next);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_CF_HEATER);
}

static void test_detect(void)
{
    sh_heater_t h = { 0 };
    /* a heater set: present, nothing to detect */
    CHECK(sh_heater_present(&h, 20));
    CHECK(!sh_heater_step(&h, 20, true));
    CHECK(!h.detected);
    /* "0 kW": no heater until the sign is seen */
    CHECK(!sh_heater_present(&h, 0));
    CHECK(!sh_heater_step(&h, 0, false));
    CHECK(!sh_heater_present(&h, 0));
    CHECK(sh_heater_step(&h, 0, true));     /* detected now (stored, shown) */
    CHECK(h.detected);
    CHECK(sh_heater_present(&h, 0));        /* the leak logic applies */
    CHECK(!sh_heater_step(&h, 0, true));    /* only once */
    CHECK(!sh_heater_step(&h, 0, false));
    CHECK(h.detected);                      /* kept (until the reset of the learned values) */
}

/* ---- guaranteed daylight (3.3) ------------------------------------------------------------- */

static void test_daylight(void)
{
    /* without learned sunrise and sunset: 09:00 - 16:00 */
    CHECK(!sh_daylight(-1, -1, HM(8, 59)));
    CHECK(sh_daylight(-1, -1, HM(9, 0)));
    CHECK(sh_daylight(-1, -1, HM(15, 59)));
    CHECK(!sh_daylight(-1, -1, HM(16, 0)));
    /* only one of them learned: still the fixed window */
    CHECK(sh_daylight(HM(6, 0), -1, HM(9, 0)));
    CHECK(!sh_daylight(-1, HM(18, 0), HM(16, 30)));
    /* learned 06:40 / 16:50: 07:40 - 15:50, ±1 min */
    const int rise = HM(6, 40), set = HM(16, 50);
    CHECK(!sh_daylight(rise, set, HM(7, 39)));
    CHECK(sh_daylight(rise, set, HM(7, 40)));
    CHECK(sh_daylight(rise, set, HM(15, 49)));
    CHECK(!sh_daylight(rise, set, HM(15, 50)));
    /* summer 05:50 / 20:30: 06:50 - 19:30 */
    CHECK(sh_daylight(HM(5, 50), HM(20, 30), HM(6, 50)));
    CHECK(!sh_daylight(HM(5, 50), HM(20, 30), HM(19, 30)));
    /* learned values that leave no window (sunrise + 1 h not before sunset - 1 h): the fixed one */
    CHECK(sh_daylight(HM(10, 0), HM(12, 0), HM(9, 0)));
    CHECK(!sh_daylight(HM(10, 0), HM(12, 0), HM(16, 0)));
    CHECK(sh_daylight(HM(10, 0), HM(11, 59), HM(9, 0)));
    CHECK(!sh_daylight(HM(10, 0), HM(12, 1), HM(9, 0)));    /* 11:00 - 11:01: learned */
    CHECK(sh_daylight(HM(10, 0), HM(12, 1), HM(11, 0)));
}

/* ---- leak checks (3.3) --------------------------------------------------------------------- */

/* A charging pump with a reliable clock, the local minute following the seconds from `t0` at
 * minute `m0`; steps of 1 s from `from` to `to` (inclusive). Returns at the first check. */
static uint32_t run_until_check(sh_leak_t *l, sh_leak_in_t *in, uint32_t t0, uint16_t m0, uint32_t from,
                                uint32_t to)
{
    for (uint32_t s = from; s <= to; s++) {
        in->min = (uint16_t)((m0 + (s - t0) / 60) % 1440);
        sh_leak_step(l, in, s);
        if (l->events & SH_LEAK_EVT_CHECK) {
            return s;
        }
    }
    return 0;
}

static sh_leak_in_t charging_in(void)
{
    const sh_leak_in_t in = {
        .heater = true, .reliable = true, .rise = -1, .set = -1, .t_col = 600, .pump_on = true,
        .charging = true,
    };
    return in;
}

static void test_schedule(void)
{
    sh_leak_t l;
    sh_leak_in_t in = charging_in();

    /* inside 09:00 - 16:00: no check, however long the pump runs */
    memset(&l, 0, sizeof(l));
    CHECK_EQ(run_until_check(&l, &in, 0, HM(9, 0), 0, 7 * 3600 - 1), 0);
    /* charging from 12:00: the time outside the window counts from 16:00 -> the check at 17:00 */
    memset(&l, 0, sizeof(l));
    CHECK_EQ(run_until_check(&l, &in, 0, HM(12, 0), 0, 6 * 3600), 5 * 3600);
    CHECK_EQ(l.phase, SH_LEAK_CHECK);
    CHECK_EQ(l.check_ref, 600);
    /* charging from 17:00: 60 min, not 59:59 */
    memset(&l, 0, sizeof(l));
    CHECK_EQ(run_until_check(&l, &in, 0, HM(17, 0), 0, 3599), 0);
    CHECK_EQ(run_until_check(&l, &in, 0, HM(17, 0), 3600, 3600), 3600);
    /* before 09:00 too */
    memset(&l, 0, sizeof(l));
    CHECK_EQ(run_until_check(&l, &in, 0, HM(7, 0), 0, 7200), 3600);
    /* a stop of the pump starts the count again */
    memset(&l, 0, sizeof(l));
    CHECK_EQ(run_until_check(&l, &in, 0, HM(17, 0), 0, 1800), 0);
    in.charging = false;
    in.pump_on = false;
    sh_leak_step(&l, &in, 1801);
    in.charging = true;
    in.pump_on = true;
    CHECK_EQ(run_until_check(&l, &in, 0, HM(17, 0), 1802, 2 * 3600), 1802 + 3600);

    /* not charging (a protection, night cooling, the test, the air venting, the probes): no check */
    memset(&l, 0, sizeof(l));
    in.charging = false;
    CHECK_EQ(run_until_check(&l, &in, 0, HM(20, 0), 0, 3 * 3600), 0);
    in.charging = true;
    /* no heater (0 kW, not detected): no check */
    memset(&l, 0, sizeof(l));
    in.heater = false;
    CHECK_EQ(run_until_check(&l, &in, 0, HM(20, 0), 0, 3 * 3600), 0);
    in.heater = true;

    /* learned 06:40 / 16:50: the window 07:40 - 15:50; the evening (14:50 - 16:50) after it:
     * charging from 12:00 -> the first check 30 min after 15:50 */
    in.rise = HM(6, 40);
    in.set = HM(16, 50);
    memset(&l, 0, sizeof(l));
    const uint32_t first = run_until_check(&l, &in, 0, HM(12, 0), 0, 6 * 3600);
    CHECK_EQ(first, (uint32_t)(HM(16, 20) - HM(12, 0)) * 60);
    /* the check passes: the time counts again — the next one in the evening after 30 min */
    in.pump_on = false;
    in.charging = false;
    for (uint32_t s = first + 1; s < first + SH_LEAK_CHECK_S; s++) {
        sh_leak_step(&l, &in, s);
    }
    CHECK_EQ(l.phase, SH_LEAK_CHECK);
    sh_leak_step(&l, &in, first + SH_LEAK_CHECK_S);
    CHECK(l.resume);
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    in.pump_on = true;
    in.charging = true;
    const uint32_t second = run_until_check(&l, &in, 0, HM(12, 0), first + SH_LEAK_CHECK_S + 1, 9 * 3600);
    CHECK_EQ(second, first + SH_LEAK_CHECK_S + SH_LEAK_EVENING_S);      /* 16:55 + 30 min */
    /* after the sunset (night, 17:30): every 60 min */
    memset(&l, 0, sizeof(l));
    CHECK_EQ(run_until_check(&l, &in, 0, HM(17, 30), 0, 2 * 3600), 3600);
    /* the evening at 16:49 counts as evening (30 min), from 16:50 on 60 min */
    memset(&l, 0, sizeof(l));
    CHECK_EQ(run_until_check(&l, &in, 0, HM(15, 50), 0, 3 * 3600), 1800);

    /* without a reliable clock: 30 min in a row in which the collector fell */
    in = charging_in();
    in.reliable = false;
    memset(&l, 0, sizeof(l));
    uint32_t s = 0;
    for (; s <= 30 * 60; s++) {
        in.t_col = (int16_t)(800 - (int)(s / 60));          /* -0.1 K a minute */
        sh_leak_step(&l, &in, s);
        if (s < 30 * 60) {
            CHECK_EQ(l.phase, SH_LEAK_IDLE);
        }
    }
    CHECK_EQ(l.phase, SH_LEAK_CHECK);
    CHECK(l.events & SH_LEAK_EVT_CHECK);
    /* a rise at minute 20 starts the count again: the check 30 min after it */
    memset(&l, 0, sizeof(l));
    uint32_t at = 0;
    for (s = 0; s <= 60 * 60 && !at; s++) {
        const int m = (int)(s / 60);
        in.t_col = (int16_t)(m < 20 ? 800 - m : m == 20 ? 790 : 790 - (m - 20));
        sh_leak_step(&l, &in, s);
        if (l.events & SH_LEAK_EVT_CHECK) {
            at = s;
        }
    }
    CHECK_EQ(at, 50 * 60);
    /* a stable collector (sun) is no fall: no check */
    memset(&l, 0, sizeof(l));
    in.t_col = 700;
    for (s = 0; s <= 3 * 3600; s++) {
        sh_leak_step(&l, &in, s);
    }
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    /* with the clock the daylight does not matter here: no clock, 12:00 */
    in.min = HM(12, 0);
    memset(&l, 0, sizeof(l));
    for (s = 0; s <= 30 * 60; s++) {
        in.t_col = (int16_t)(800 - (int)(s / 60));
        sh_leak_step(&l, &in, s);
    }
    CHECK_EQ(l.phase, SH_LEAK_CHECK);
}

/* A check started at `t0` with the collector at 60.0 °C; it falls by `drop` over the 5 min. */
static void check_with(sh_leak_t *l, int16_t drop, uint32_t t0)
{
    memset(l, 0, sizeof(*l));
    sh_leak_in_t in = charging_in();
    in.min = HM(20, 0);
    l->counting = true;
    l->run_s = t0 - 3600;
    sh_leak_step(l, &in, t0);
    CHECK_EQ(l->phase, SH_LEAK_CHECK);
    in.pump_on = false;
    in.charging = false;
    for (uint32_t s = t0 + 1; s <= t0 + SH_LEAK_CHECK_S; s++) {
        in.t_col = (int16_t)(600 - (int32_t)drop * (int32_t)(s - t0) / (int32_t)SH_LEAK_CHECK_S);
        sh_leak_step(l, &in, s);
    }
}

static void test_check(void)
{
    sh_leak_t l;
    /* 1.9 K: the sun — the pump goes on at once */
    check_with(&l, 19, 10000);
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    CHECK(l.resume);
    CHECK(l.events & SH_LEAK_EVT_PASS);
    CHECK(l.last && !l.last_stop);
    CHECK_EQ(l.last_drop, 19);
    /* a rising collector too */
    check_with(&l, -15, 10000);
    CHECK(l.resume);
    CHECK_EQ(l.last_drop, -15);
    /* 2.0 K: the heat comes from the tank — stopped */
    check_with(&l, 20, 10000);
    CHECK_EQ(l.phase, SH_LEAK_STOP);
    CHECK(!l.resume);
    CHECK(l.events & SH_LEAK_EVT_STOP);
    CHECK(l.last && l.last_stop);
    CHECK_EQ(l.last_drop, 20);
    /* the check lasts 5 min: one second before, still running */
    memset(&l, 0, sizeof(l));
    sh_leak_in_t in = charging_in();
    in.min = HM(20, 0);
    l.counting = true;
    l.run_s = 0;
    sh_leak_step(&l, &in, 5000);
    CHECK_EQ(sh_leak_left(&l, 5000), SH_LEAK_CHECK_S);
    in.pump_on = false;
    in.t_col = 500;
    sh_leak_step(&l, &in, 5000 + SH_LEAK_CHECK_S - 1);
    CHECK_EQ(l.phase, SH_LEAK_CHECK);
    CHECK_EQ(sh_leak_left(&l, 5000 + SH_LEAK_CHECK_S - 1), 1);
    /* the pump runs during the check (a protection, the test ...): given up */
    memset(&l, 0, sizeof(l));
    in = charging_in();
    in.min = HM(20, 0);
    l.counting = true;
    sh_leak_step(&l, &in, 5000);
    in.pump_on = false;
    sh_leak_step(&l, &in, 5001);
    in.pump_on = true;
    sh_leak_step(&l, &in, 5060);
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    CHECK(l.events & SH_LEAK_EVT_ABORT);
    CHECK(!l.resume);
    /* the collector value lost: given up */
    memset(&l, 0, sizeof(l));
    in = charging_in();
    in.min = HM(20, 0);
    l.counting = true;
    sh_leak_step(&l, &in, 5000);
    in.pump_on = false;
    in.t_col = SH_TEMP_INVALID;
    sh_leak_step(&l, &in, 5001);
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    CHECK(l.events & SH_LEAK_EVT_ABORT);
}

static void test_new_start(void)
{
    sh_leak_t l;
    sh_leak_in_t in = charging_in();
    in.pump_on = false;
    in.charging = false;
    in.min = HM(21, 0);

    /* stopped: the collector warms 1.9 K in 10 min — not enough; 2.0 K — the stop ends */
    check_with(&l, 20, 10000);
    uint32_t s = 10000 + SH_LEAK_CHECK_S + 1;
    for (uint32_t k = 0; k <= 600; k++, s++) {
        in.t_col = (int16_t)(580 + 19 * (int32_t)k / 600);
        sh_leak_step(&l, &in, s);
    }
    CHECK_EQ(l.phase, SH_LEAK_STOP);
    check_with(&l, 20, 10000);
    s = 10000 + SH_LEAK_CHECK_S + 1;
    for (uint32_t k = 0; k <= 600; k++, s++) {
        in.t_col = (int16_t)(580 + 20 * (int32_t)k / 600);
        sh_leak_step(&l, &in, s);
        if (k < 600) {
            CHECK_EQ(l.phase, SH_LEAK_STOP);
        }
    }
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    CHECK(l.events & SH_LEAK_EVT_END);
    CHECK(!l.resume);                       /* the difference decides, with the minimum times */
    /* a pump run (a protection) during the 10 min starts the warming window again */
    check_with(&l, 20, 10000);
    s = 10000 + SH_LEAK_CHECK_S + 1;
    for (uint32_t k = 0; k <= 600; k++, s++) {
        in.t_col = (int16_t)(580 + 20 * (int32_t)k / 600);
        in.pump_on = k == 300;
        sh_leak_step(&l, &in, s);
    }
    CHECK_EQ(l.phase, SH_LEAK_STOP);
    in.pump_on = false;
    /* the next sunrise ends it */
    check_with(&l, 25, 10000);
    in.t_col = 300;
    sh_leak_step(&l, &in, 30000);
    CHECK_EQ(l.phase, SH_LEAK_STOP);
    in.sunrise = true;
    sh_leak_step(&l, &in, 30001);
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    CHECK(l.events & SH_LEAK_EVT_END);
    in.sunrise = false;
    /* the guaranteed daylight begins (a reliable clock): the stop ends — in a cloudy morning the
     * collector may not warm by 2 K in 10 min */
    check_with(&l, 25, 10000);
    in.t_col = 300;
    in.min = HM(8, 59);
    sh_leak_step(&l, &in, 30000);
    CHECK_EQ(l.phase, SH_LEAK_STOP);
    in.min = HM(9, 0);
    sh_leak_step(&l, &in, 30001);
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    CHECK(l.events & SH_LEAK_EVT_END);
    check_with(&l, 25, 10000);                      /* without a reliable clock: not by the time */
    in.reliable = false;
    sh_leak_step(&l, &in, 30000);
    CHECK_EQ(l.phase, SH_LEAK_STOP);
    in.reliable = true;
    in.min = HM(21, 0);
    /* the heater set to 0 kW (not detected): the stop ends */
    check_with(&l, 25, 10000);
    in.heater = false;
    sh_leak_step(&l, &in, 30000);
    CHECK_EQ(l.phase, SH_LEAK_IDLE);
    CHECK(l.events & SH_LEAK_EVT_END);
}

/* The pump rules: the check stops a charging pump at once, the stop blocks the difference only. */
static void test_pump_rules(void)
{
    sh_pump_state_t st;
    sh_pump_params_t p = P;

    /* charging (60 °C / 40 °C) for 1 min: the check stops it at once (no minimum run time) */
    memset(&st, 0, sizeof(st));
    sh_pump_decide(&p, &st, 600, 400, 3600);
    CHECK(st.on);
    p.leak_check = true;
    sh_pump_decide(&p, &st, 600, 400, 60);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_LEAK_CHECK);
    CHECK_EQ(strcmp(sh_pump_reason_name(SH_PUMP_REASON_LEAK_CHECK), "heater leak check"), 0);
    /* the protections act during the check: collector protection, frost */
    sh_pump_decide(&p, &st, 1100, 400, 60);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COL_PROTECT);
    memset(&st, 0, sizeof(st));
    sh_pump_decide(&p, &st, 30, 400, 600);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_FROST);
    /* Tmax first */
    memset(&st, 0, sizeof(st));
    sh_pump_decide(&p, &st, 900, 700, 600);
    CHECK_EQ(st.reason, SH_PUMP_REASON_TANK_MAX);

    /* the check passed: the pump goes on at once — no minimum off time (1 s) and no minimum
     * collector — while the difference is above the switch-off one (4 K) */
    p.leak_check = false;
    p.leak_resume = true;
    p.col_min = 500;
    memset(&st, 0, sizeof(st));
    sh_pump_decide(&p, &st, 450, 400, 1);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_DIFF);
    memset(&st, 0, sizeof(st));
    sh_pump_decide(&p, &st, 440, 400, 1);
    CHECK(!st.on);
    /* without the resume the same is a stopped pump: minimum off time, collector minimum, 6 K */
    p.leak_resume = false;
    memset(&st, 0, sizeof(st));
    sh_pump_decide(&p, &st, 450, 400, 1);
    CHECK(!st.on);
    p.col_min = 250;

    /* the leak stopped: no start by the difference ... */
    p.leak_stop = true;
    memset(&st, 0, sizeof(st));
    sh_pump_decide(&p, &st, 800, 400, 3600);
    CHECK(!st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_LEAK_STOP);
    /* ... but the protections and the night cooling run */
    sh_pump_decide(&p, &st, 1100, 400, 3600);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COL_PROTECT);
    p.cool = true;
    memset(&st, 0, sizeof(st));
    sh_pump_decide(&p, &st, 300, 600, 3600);
    CHECK(st.on);
    CHECK_EQ(st.reason, SH_PUMP_REASON_COOL);

    /* the sensor-fault modes are above it: the check / stop do not change the probes */
    sh_pump_ctl_t c;
    memset(&c, 0, sizeof(c));
    p = P;
    p.leak_stop = true;
    const sh_pump_temps_t t = { .t_col = SH_TEMP_INVALID, .t_tank = 400, .col_fault = true };
    sh_pump_ctl_step(&c, &p, &t, 1000);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_CF_PROBE);
    /* the test too */
    memset(&c, 0, sizeof(c));
    p.leak_check = true;
    const sh_pump_temps_t t2 = { .t_col = 600, .t_tank = 400 };
    sh_pump_ctl_plus(&c, 300, 1000);
    sh_pump_ctl_step(&c, &p, &t2, 1000);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_TEST);
}

/* ---- E8 (3.4) ------------------------------------------------------------------------------ */

static void test_e8_threshold(void)
{
    /* the example of the document: 1.8 m² flat, 120 l -> 288 W -> 2.06 K/h -> half 1.03 -> 1.1 K/h */
    CHECK_EQ(sh_e8_threshold(false, 18, 120), 11);
    /* the defaults: 2.0 m² flat, 150 l -> 320 W -> 1.83 K/h -> 0.92 -> 1.0 */
    CHECK_EQ(sh_e8_threshold(false, 20, 150), 10);
    /* evacuated tubes 1.5 W/m²K: 2.0 m², 150 l -> 120 W -> 0.69 K/h -> 0.34 -> 0.4 */
    CHECK_EQ(sh_e8_threshold(true, 20, 150), 4);
}

/* One night hour, a sample a minute from `t0`: the collector from `c0` by `dc`, the tank from `k0`
 * by `dk` (linear). */
static void hour(sh_e8_t *e, sh_e8_in_t *in, uint32_t t0, int16_t c0, int16_t dc, int16_t k0, int16_t dk)
{
    for (uint32_t m = 0; m <= 60; m++) {
        in->t_col = (int16_t)(c0 + dc * (int32_t)m / 60);
        in->t_tank = (int16_t)(k0 + dk * (int32_t)m / 60);
        sh_e8_step(e, in, t0 + m * 60);
    }
}

/* Is the sign met in one hour of the night with these changes? (a new night, nothing learned) */
static bool sign_in(int16_t c0, int16_t dc, int16_t k0, int16_t dk, int16_t normal)
{
    sh_e8_t e;
    memset(&e, 0, sizeof(e));
    sh_e8_learned_reset(&e.l);
    if (normal != SH_E8_NO_RATE) {
        e.l.rate[SH_E8_DAYS - 1] = normal;
    }
    sh_e8_in_t in = { .night = true, .threshold = 10 };
    hour(&e, &in, 1000, c0, dc, k0, dk);
    return e.sign;
}

static void test_e8_sign(void)
{
    /* the collector rises 2.0 K, the tank falls 1.0 K (the threshold), they come closer */
    CHECK(sign_in(200, 20, 600, -10, SH_E8_NO_RATE));
    /* the collector rises 1.9 K: no */
    CHECK(!sign_in(200, 19, 600, -10, SH_E8_NO_RATE));
    /* the tank falls 0.9 K (below the threshold): no */
    CHECK(!sign_in(200, 20, 600, -9, SH_E8_NO_RATE));
    /* faster than the learned normal cooling: 1.0 K/h against 0.9 / 1.0 */
    CHECK(sign_in(200, 20, 600, -10, 9));
    CHECK(!sign_in(200, 20, 600, -10, 10));
    CHECK(sign_in(200, 20, 600, -15, 14));
    /* the difference must shrink: a collector above the tank that rises moves away */
    CHECK(!sign_in(650, 20, 600, -10, SH_E8_NO_RATE));
    /* (consumption: the tank falls fast, the collector cools — no) */
    CHECK(!sign_in(200, -10, 600, -50, SH_E8_NO_RATE));

    /* at least 1 h with the pump stopped: 59 min is not enough */
    sh_e8_t e;
    memset(&e, 0, sizeof(e));
    sh_e8_learned_reset(&e.l);
    sh_e8_in_t in = { .night = true, .threshold = 10 };
    for (uint32_t m = 0; m <= 59; m++) {
        in.t_col = (int16_t)(200 + m);
        in.t_tank = (int16_t)(600 - m);
        sh_e8_step(&e, &in, 1000 + m * 60);
    }
    CHECK(!e.sign && !e.hour);
    /* a pump run starts the hour again */
    in.pump_on = true;
    sh_e8_step(&e, &in, 1000 + 60 * 60);
    in.pump_on = false;
    hour(&e, &in, 1000 + 61 * 60, 200, 20, 600, -10);
    CHECK(e.sign);
    /* not at night: nothing */
    memset(&e, 0, sizeof(e));
    sh_e8_learned_reset(&e.l);
    in.night = false;
    hour(&e, &in, 1000, 200, 30, 600, -20);
    CHECK(!e.sign && !e.hour);
    /* a faulty sensor (no value): nothing */
    in.night = true;
    memset(&e, 0, sizeof(e));
    sh_e8_learned_reset(&e.l);
    for (uint32_t m = 0; m <= 60; m++) {
        in.t_col = m == 30 ? SH_TEMP_INVALID : (int16_t)(200 + m);
        in.t_tank = (int16_t)(600 - m);
        sh_e8_step(&e, &in, 1000 + m * 60);
    }
    CHECK(!e.sign && !e.hour);
}

/* One whole night: dusk (day), `kind` 0 normal (the tank falls 0.5 K/h, the collector cools),
 * 1 with the sign, 2 not counted (the pump runs all night); then the morning. */
static void night(sh_e8_t *e, uint32_t *t, int kind)
{
    sh_e8_in_t in = { .night = false, .t_col = 300, .t_tank = 600, .threshold = 10 };
    sh_e8_step(e, &in, *t);
    *t += 60;
    in.night = true;
    for (int h = 0; h < 3; h++) {
        if (kind == 1) {
            hour(e, &in, *t, (int16_t)(200 + 20 * h), 20, (int16_t)(600 - 15 * h), -15);
        } else if (kind == 0) {
            hour(e, &in, *t, (int16_t)(200 - 5 * h), -5, (int16_t)(600 - 5 * h), -5);
        } else {
            in.pump_on = true;
            hour(e, &in, *t, 200, 0, 600, 0);
        }
        *t += 61 * 60;
    }
    in.night = false;
    in.pump_on = false;
    sh_e8_step(e, &in, *t);
    *t += 60;
}

static void test_e8_nights(void)
{
    sh_e8_t e;
    memset(&e, 0, sizeof(e));
    sh_e8_learned_reset(&e.l);
    uint32_t t = 1000;

    /* 2 nights with the sign, 1 not counted, the 3rd with the sign: declared */
    night(&e, &t, 1);
    CHECK(e.events & SH_E8_EVT_NIGHT);
    CHECK_EQ(e.l.sign_nights, 1);
    night(&e, &t, 1);
    CHECK(!e.l.active);
    night(&e, &t, 2);
    CHECK(!(e.events & SH_E8_EVT_NIGHT));
    CHECK_EQ(e.l.sign_nights, 2);
    night(&e, &t, 1);
    CHECK_EQ(e.l.sign_nights, 3);
    CHECK(e.l.active);
    CHECK(e.events & SH_E8_EVT_CHANGED);
    /* 2 normal nights, then one with the sign: stays, the count starts again */
    night(&e, &t, 0);
    night(&e, &t, 0);
    CHECK_EQ(e.l.normal_nights, 2);
    CHECK(e.l.active);
    night(&e, &t, 1);
    CHECK_EQ(e.l.normal_nights, 0);
    CHECK(e.l.active);
    /* 3 normal nights in a row: cleared */
    night(&e, &t, 0);
    night(&e, &t, 0);
    CHECK(e.l.active);
    night(&e, &t, 0);
    CHECK(!e.l.active);
    CHECK_EQ(e.l.sign_nights, 0);
    /* 2 nights with the sign after a normal one: not yet */
    night(&e, &t, 1);
    night(&e, &t, 1);
    CHECK(!e.l.active);

    /* the learned normal cooling: 5 normal nights above, each 0.5 K/h */
    CHECK_EQ(sh_e8_normal(&e.l), 5);
    int n = 0;
    for (int i = 0; i < SH_E8_DAYS; i++) {
        n += e.l.rate[i] != SH_E8_NO_RATE;
    }
    CHECK_EQ(n, 5);
    /* 7 at most, newest last */
    for (int i = 0; i < 4; i++) {
        night(&e, &t, 0);
    }
    for (int i = 0; i < SH_E8_DAYS; i++) {
        CHECK_EQ(e.l.rate[i], 5);
    }
    /* the heater minutes are not taken (the tank rises), the night is still normal */
    sh_e8_in_t in = { .night = false, .t_col = 300, .t_tank = 600, .threshold = 10 };
    sh_e8_step(&e, &in, t);
    in.night = true;
    hour(&e, &in, t + 60, 200, -5, 600, -5);            /* 0.5 K/h */
    in.heater = true;
    hour(&e, &in, t + 62 * 60, 195, -5, 595, 30);       /* the heater: +3 K, not taken */
    in.heater = false;
    hour(&e, &in, t + 124 * 60, 190, -5, 625, -5);
    in.night = false;
    sh_e8_step(&e, &in, t + 190 * 60);
    CHECK(e.events & SH_E8_EVT_NIGHT);
    CHECK(e.l.rate[SH_E8_DAYS - 1] >= 4 && e.l.rate[SH_E8_DAYS - 1] <= 6);
}

/* The code: after E6, before the information; red double blink; the history bit. */
static void test_e8_code(void)
{
    sh_alarm_state_t st;
    memset(&st, 0, sizeof(st));
    const sh_alarm_params_t ap = { .e6_time_s = 600, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200,
                                   .e6_clear_diff = 100 };
    sh_alarm_in_t in = { .t_col = 200, .t_tank = 600, .e8 = true };
    const uint8_t a = sh_alarm_step(&st, &ap, &in, 1000);
    CHECK_EQ(a, SH_ALARM_BIT(SH_ALARM_E8));
    CHECK_EQ(strcmp(sh_alarm_name(SH_ALARM_E8), "E8"), 0);
    /* the bits before it are kept (daily records) */
    CHECK_EQ(SH_ALARM_BIT(SH_ALARM_HOT), 0x20);
    CHECK_EQ(SH_ALARM_BIT(SH_ALARM_FRO), 0x40);
    CHECK_EQ(SH_ALARM_BIT(SH_ALARM_E8), 0x80);
    /* the display: E6, E8, Hot */
    const uint8_t three = SH_ALARM_BIT(SH_ALARM_E6) | SH_ALARM_BIT(SH_ALARM_E8) | SH_ALARM_BIT(SH_ALARM_HOT);
    CHECK_EQ(sh_alarm_code_at(three, 0), SH_ALARM_E6);
    CHECK_EQ(sh_alarm_code_at(three, SH_ALARM_CODE_MS), SH_ALARM_E8);
    CHECK_EQ(sh_alarm_code_at(three, 2 * SH_ALARM_CODE_MS), SH_ALARM_HOT);
    CHECK_EQ(sh_alarm_code_at(three, 3 * SH_ALARM_CODE_MS), SH_ALARM_COUNT);
    CHECK_EQ(sh_alarm_count(0xFF), 8);
    /* the LED: red double blink */
    sh_ind_state_t is = { 0 };
    const sh_ind_params_t ip = { .cold = 300, .hot = 600 };
    const sh_ind_in_t ii = { .alarms = SH_ALARM_BIT(SH_ALARM_E8), .t_col = 200, .wifi_connected = true };
    const sh_ind_out_t o = sh_ind_update(&is, &ip, &ii);
    CHECK_EQ(o.color, SH_COLOR_RED);
    CHECK_EQ(o.pattern, SH_IND_DOUBLE);
    /* the CSV: in the display order */
    sh_hist_rec_t r;
    sh_hist_rec_init(&r, sh_day_number(2026, 11, 12));
    r.alarms = SH_ALARM_BIT(SH_ALARM_HOT) | SH_ALARM_BIT(SH_ALARM_E8) | SH_ALARM_BIT(SH_ALARM_E1);
    r.heater_min = 95;
    char line[SH_HIST_CSV_LINE_MAX];
    CHECK(sh_hist_csv_line(line, sizeof(line), &r) > 0);
    CHECK(strstr(line, ";0;0;0;0;95;E1 E8 Hot;") != NULL);
    /* encoded and decoded */
    uint8_t b[SH_HIST_REC_SIZE];
    sh_hist_encode(&r, b);
    sh_hist_rec_t back;
    CHECK_EQ(sh_hist_decode(b, &back), SH_HIST_VALID);
    CHECK_EQ(back.alarms, r.alarms);
    CHECK_EQ(back.heater_min, 95);
}

/* ---- the heater minutes of the day (3.2) --------------------------------------------------- */

static void test_day(void)
{
    sh_day_t d;
    sh_day_init(&d);
    sh_local_t lt = { .set = true, .day = sh_day_number(2026, 11, 12), .min = 0 };
    sh_day_in_t in = { .t_col = 200, .t_tank = 500 };
    sh_day_sum_t done;
    /* from 00:00: 90 min with the sign, then off */
    for (uint32_t s = 0; s < 1440 * 60; s += 10) {
        lt.min = (uint16_t)(s / 60);
        in.heater = s >= 120 * 60 && s < 210 * 60;
        CHECK(!sh_day_step(&d, &in, &lt, 1000 + s, &done));
    }
    lt.day++;
    lt.min = 0;
    CHECK(sh_day_step(&d, &in, &lt, 1000 + 1440 * 60, &done));
    CHECK_EQ(done.heater_s, 90 * 60);
    CHECK_EQ(done.run_s[SH_RUN_CHARGE], 0);
}

int main(void)
{
    test_installer();
    test_rise();
    test_sign();
    test_detect();
    test_daylight();
    test_schedule();
    test_check();
    test_new_start();
    test_pump_rules();
    test_e8_threshold();
    test_e8_sign();
    test_e8_nights();
    test_e8_code();
    test_day();
    return test_summary("test_group_d");
}
