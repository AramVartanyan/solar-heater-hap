/* Host test of the pulse mode (doc/operating-modes.md 2.1, p. 16): the pulse mode — the mounting in
 * the installer data (sh_installer); when a pulse is due: the window, the day intervals, the hot
 * collector, the night, the night cooling and the vacation, the frost watch (sh_pulse, sh_cool);
 * the pulse, the reliable values and the decisions by them, the heater sign on the pipe
 * (sh_pump_ctl, sh_pump_logic); no heater leak, E8 on the pipes, E6 not by a pulse, the run kind of
 * the pulse (sh_heater, sh_e8, sh_alarm, sh_day); the sleeves unchanged.
 * Temperatures are tenths of °C, times seconds; local time in minutes of the day. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_pulse.h"
#include "sh_pump_ctl.h"
#include "sh_cool.h"
#include "sh_sun.h"
#include "sh_heater.h"
#include "sh_e8.h"
#include "sh_installer.h"
#include "sh_alarm.h"
#include "sh_day.h"

#define HM(h, m)    ((uint16_t)((h) * 60 + (m)))
#define MIN         60u
#define HOUR        3600u

/* The defaults: differences 6 / 4 K, Tmax 70 °C, C.Lo 25 °C, collector protection 110 °C (ceiling
 * 115 °C), boiling point 127.7 °C, frost threshold 4 °C; the pulse mode: minimum run time 30 s. */
static const sh_pump_params_t PP = {
    .diff_on = 60, .diff_off = 40, .min_run_s = SH_PULSE_RUN_S, .min_off_s = 120, .tank_max = 700,
    .col_min = 250, .col_protect = 1100, .col_protect_cap = 1150, .col_boiling = 1277, .frost_on = true,
    .frost = 40, .pulse = true,
};

/* ---- installer data: the mounting (3.1) ---------------------------------------------------- */

static void test_installer(void)
{
    sh_installer_t d = { .v = {
        [SH_INST_FLUID] = SH_FLUID_WATER, [SH_INST_PRESSURE] = 15, [SH_INST_COL_MAX] = 120,
        [SH_INST_COL_TYPE] = SH_COL_FLAT, [SH_INST_COL_AREA] = 20, [SH_INST_TANK_VOL] = 150,
        [SH_INST_HEATER] = 20, [SH_INST_MOUNT] = SH_MOUNT_SLEEVES,
    } };
    CHECK_EQ(strcmp(sh_inst_info[SH_INST_MOUNT].key, "mount"), 0);
    CHECK(sh_installer_ok(&d));
    d.v[SH_INST_MOUNT] = SH_MOUNT_PIPES;
    CHECK(sh_installer_ok(&d));
    d.v[SH_INST_MOUNT] = 2;
    CHECK(!sh_installer_ok(&d));
    sh_installer_t def = d;
    def.v[SH_INST_MOUNT] = SH_MOUNT_SLEEVES;
    CHECK_EQ(sh_installer_sanitize(&d, &def), 1u << SH_INST_MOUNT);
    CHECK_EQ(d.v[SH_INST_MOUNT], SH_MOUNT_SLEEVES);
}

/* ---- when a pulse is due (3.2, 3.3) -------------------------------------------------------- */

/* A reliable clock without learned sun times, the day 100, a reliable value of 30 °C at 1000 s. */
static sh_pulse_in_t pin(uint16_t min)
{
    const sh_pulse_in_t in = {
        .reliable = true, .lt = { .set = true, .day = 100, .min = min }, .rise = -1, .set = -1,
        .pipe_col = 300, .rel = true, .rel_col = 300, .rel_s = 1000, .frost_on = true, .frost = 40,
        .boiling = 1277,
    };
    return in;
}

static bool due_at(sh_pulse_in_t in, uint32_t now)
{
    sh_pulse_t p = { 0 };
    sh_pulse_step(&p, &in, now);
    return p.due;
}

static void test_window(void)
{
    /* with a reliable clock 08:00 - 18:00 until the sunrise and the sunset are learned (±1 min) */
    CHECK(!sh_pulse_window_at(-1, -1, HM(7, 59)));
    CHECK(sh_pulse_window_at(-1, -1, HM(8, 0)));
    CHECK(sh_pulse_window_at(-1, -1, HM(17, 59)));
    CHECK(!sh_pulse_window_at(-1, -1, HM(18, 0)));
    /* only one of them learned: still 08:00 - 18:00 */
    CHECK(!sh_pulse_window_at(HM(5, 50), -1, HM(7, 0)));
    CHECK(sh_pulse_window_at(-1, HM(21, 10), HM(17, 0)));
    CHECK(!sh_pulse_window_at(-1, HM(21, 10), HM(19, 0)));
    /* then from the learned sunrise to the learned sunset (June ~05:50 - 21:10) */
    CHECK(!sh_pulse_window_at(HM(5, 50), HM(21, 10), HM(5, 49)));
    CHECK(sh_pulse_window_at(HM(5, 50), HM(21, 10), HM(5, 50)));
    CHECK(sh_pulse_window_at(HM(5, 50), HM(21, 10), HM(21, 9)));
    CHECK(!sh_pulse_window_at(HM(5, 50), HM(21, 10), HM(21, 10)));
    /* December ~07:50 - 16:45 */
    CHECK(!sh_pulse_window_at(HM(7, 50), HM(16, 45), HM(7, 49)));
    CHECK(sh_pulse_window_at(HM(7, 50), HM(16, 45), HM(16, 44)));
    CHECK(!sh_pulse_window_at(HM(7, 50), HM(16, 45), HM(16, 45)));

    /* the step: the learned times with a reliable clock */
    sh_pulse_t p = { 0 };
    sh_pulse_in_t in = pin(HM(7, 0));
    in.rise = HM(6, 30);
    in.set = HM(20, 0);
    sh_pulse_step(&p, &in, 2000);
    CHECK(p.window);
    in.lt.min = HM(6, 29);
    sh_pulse_step(&p, &in, 2000);
    CHECK(!p.window);
    /* without a reliable clock: from the sunrise to the sunset detected on the pipes */
    in = pin(HM(12, 0));
    in.reliable = false;
    sh_pulse_step(&p, &in, 2000);
    CHECK(!p.window);
    in.risen = true;
    sh_pulse_step(&p, &in, 2000);
    CHECK(p.window);
    /* an unreliable clock counts as none */
    in = pin(HM(3, 0));
    in.reliable = false;
    in.risen = true;
    sh_pulse_step(&p, &in, 2000);
    CHECK(p.window);
}

/* The start without a clock: sh_sun on the collector pipe, 2 K in 10 min with the pump stopped. */
static void test_sun_start(void)
{
    sh_sun_t s = { 0 };
    sh_sun_days_reset(&s.days);
    const sh_local_t none = { 0 };
    uint32_t t = 0;
    for (int i = 0; i <= 10; i++, t += 60) {
        sh_sun_step(&s, (int16_t)(50 + i * 1), 200, false, &none, false, t);   /* 1 K in 10 min */
    }
    CHECK(!s.risen);
    uint8_t evt = 0;
    for (int i = 0; i <= 10; i++, t += 60) {
        evt |= sh_sun_step(&s, (int16_t)(60 + i * 2), 200, false, &none, false, t);    /* 2 K */
    }
    CHECK(evt & SH_SUN_EVT_RISE);
    CHECK(s.risen);
    /* the sunset (the collector pipe below the tank pipe after it was above) ends it */
    sh_sun_step(&s, 400, 300, false, &none, false, t += 60);
    evt = sh_sun_step(&s, 290, 300, false, &none, false, t += 60);
    CHECK(evt & SH_SUN_EVT_SET);
    CHECK(!s.risen);

    /* the sunset also by the values of a pulse after 30 s (the pump runs: `flow`), not before */
    sh_sun_step(&s, 400, 300, false, &none, false, t += 60);                /* above again */
    evt = sh_sun_step_flow(&s, 290, 300, true, false, &none, false, t += 60);  /* running, < 30 s */
    CHECK(!(evt & SH_SUN_EVT_SET));
    evt = sh_sun_step_flow(&s, 290, 300, true, true, &none, false, t += 60);   /* the pulse values */
    CHECK(evt & SH_SUN_EVT_SET);
}

static void test_day_intervals(void)
{
    /* below 50 °C 30 min, 50 - 80 °C 10 min, above 80 °C 3 min */
    CHECK_EQ(sh_pulse_day_s(499), 30 * MIN);
    CHECK_EQ(sh_pulse_day_s(500), 10 * MIN);
    CHECK_EQ(sh_pulse_day_s(800), 10 * MIN);
    CHECK_EQ(sh_pulse_day_s(801), 3 * MIN);
    CHECK_EQ(sh_pulse_day_s(SH_TEMP_INVALID), 30 * MIN);

    /* in the window, counted from the last reliable value */
    sh_pulse_in_t in = pin(HM(12, 0));
    in.rel_col = 499;
    CHECK(!due_at(in, 1000 + 30 * MIN - 1));
    CHECK(due_at(in, 1000 + 30 * MIN));
    in.rel_col = 500;
    CHECK(!due_at(in, 1000 + 10 * MIN - 1));
    CHECK(due_at(in, 1000 + 10 * MIN));
    in.rel_col = 800;
    CHECK(due_at(in, 1000 + 10 * MIN));
    CHECK(!due_at(in, 1000 + 10 * MIN - 1));
    in.rel_col = 801;
    CHECK(!due_at(in, 1000 + 3 * MIN - 1));
    CHECK(due_at(in, 1000 + 3 * MIN));
    /* the pump runs: no pulse */
    in.pump_on = true;
    CHECK(!due_at(in, 1000 + 3 * MIN));
    /* no reliable value yet (boot, the mounting changed): a pulse at once */
    in = pin(HM(2, 0));
    in.rel = false;
    sh_pulse_t p = { 0 };
    sh_pulse_step(&p, &in, 5);
    CHECK(p.due);
    CHECK_EQ(p.next_s, 5);
}

static void test_night(void)
{
    /* outside the window: 3 h after the last reliable value (the collector pipe far above the gate) */
    sh_pulse_in_t in = pin(HM(22, 0));
    in.rel_col = 200;
    in.pipe_col = 200;
    sh_pulse_t p = { 0 };
    sh_pulse_step(&p, &in, 1000 + 3 * HOUR - 1);
    CHECK(!p.due);
    CHECK_EQ(p.next_s, 1000 + 3 * HOUR);
    sh_pulse_step(&p, &in, 1000 + 3 * HOUR);
    CHECK(p.due);
    /* a cooling or frost pulse renews the value: the 3 h count from it */
    in.rel_s = 1000 + 2 * HOUR;
    sh_pulse_step(&p, &in, 1000 + 3 * HOUR);
    CHECK(!p.due);
    CHECK_EQ(p.next_s, 1000 + 5 * HOUR);
    /* the night cooling / the vacation (a session runs or may start): 30 min */
    in.rel_s = 1000;
    in.cool = true;
    CHECK(!due_at(in, 1000 + 30 * MIN - 1));
    CHECK(due_at(in, 1000 + 30 * MIN));
    /* in the window the cooling does not matter (the day interval) */
    in = pin(HM(12, 0));
    in.cool = true;
    in.rel_col = 900;
    CHECK(due_at(in, 1000 + 3 * MIN));
}

static void test_hot(void)
{
    /* a reliable collector at the boiling point - 10 K (117.7 °C): no pulses */
    sh_pulse_t p = { 0 };
    sh_pulse_in_t in = pin(HM(13, 0));
    in.reliable = false;
    in.lt.set = false;
    in.pump_on = true;
    in.fresh = true;
    in.rel_col = 1176;
    sh_pulse_step(&p, &in, 1000);
    CHECK(!p.hot);
    in.rel_col = 1177;
    sh_pulse_step(&p, &in, 1000);
    CHECK(p.hot);
    CHECK(p.events & SH_PULSE_EVT_HOT);
    /* the pump stops; no pulse whatever the interval; nothing planned */
    in.pump_on = false;
    in.fresh = false;
    sh_pulse_step(&p, &in, 1000 + 3 * HOUR);
    CHECK(!p.due);
    CHECK_EQ(p.next_s, SH_PULSE_NONE);
    /* without a reliable clock: not earlier than 5 h */
    sh_pulse_step(&p, &in, 1000 + 5 * HOUR - 1);
    CHECK(p.hot);
    sh_pulse_step(&p, &in, 1000 + 5 * HOUR);
    CHECK(!p.hot);
    CHECK(p.events & SH_PULSE_EVT_HOT_END);
    CHECK(p.due);                       /* the interval is long over */

    /* with a reliable clock: the learned sunset (±1 min) */
    sh_pulse_t q = { 0 };
    in = pin(HM(13, 0));
    in.set = HM(20, 0);
    in.rise = HM(6, 0);
    in.pump_on = true;
    in.fresh = true;
    in.rel_col = 1200;
    sh_pulse_step(&q, &in, 1000);
    CHECK(q.hot);
    in.pump_on = false;
    in.fresh = false;
    in.lt.min = HM(19, 59);
    sh_pulse_step(&q, &in, 1000 + 7 * HOUR);
    CHECK(q.hot);
    in.lt.min = HM(20, 0);
    sh_pulse_step(&q, &in, 1000 + 7 * HOUR);
    CHECK(!q.hot);
    /* the learned sunset comes before 5 h: it counts */
    q = (sh_pulse_t){ 0 };
    in.pump_on = true;
    in.fresh = true;
    in.lt.min = HM(18, 0);
    sh_pulse_step(&q, &in, 1000);
    in.pump_on = false;
    in.fresh = false;
    in.lt.min = HM(20, 0);
    sh_pulse_step(&q, &in, 1000 + 2 * HOUR);
    CHECK(!q.hot);

    /* a reliable clock without a learned sunset: 18:00 and not earlier than 5 h — the later one */
    q = (sh_pulse_t){ 0 };
    in = pin(HM(15, 0));
    in.pump_on = true;
    in.fresh = true;
    in.rel_col = 1200;
    sh_pulse_step(&q, &in, 1000);
    in.pump_on = false;
    in.fresh = false;
    in.lt.min = HM(18, 0);              /* 18:00, but only 3 h */
    sh_pulse_step(&q, &in, 1000 + 3 * HOUR);
    CHECK(q.hot);
    in.lt.min = HM(19, 59);
    sh_pulse_step(&q, &in, 1000 + 5 * HOUR - 60);
    CHECK(q.hot);
    in.lt.min = HM(20, 0);
    sh_pulse_step(&q, &in, 1000 + 5 * HOUR);
    CHECK(!q.hot);
    /* 5 h, but before 18:00 */
    q = (sh_pulse_t){ 0 };
    in.lt.min = HM(10, 0);
    in.pump_on = true;
    in.fresh = true;
    sh_pulse_step(&q, &in, 1000);
    in.pump_on = false;
    in.fresh = false;
    in.lt.min = HM(17, 59);
    sh_pulse_step(&q, &in, 1000 + 8 * HOUR);
    CHECK(q.hot);
    in.lt.min = HM(18, 0);
    sh_pulse_step(&q, &in, 1000 + 8 * HOUR);
    CHECK(!q.hot);
}

/* The frost watch: the gate (threshold + 5 K = 9 °C) only starts it. */
static void test_frost(void)
{
    sh_pulse_t p = { 0 };
    sh_pulse_in_t in = pin(HM(22, 0));
    in.rel_col = 150;
    in.pipe_col = 95;                   /* above the gate: no watch, the night pulse */
    sh_pulse_step(&p, &in, 2000);
    CHECK(!p.watch);
    CHECK(!p.due);
    CHECK_EQ(p.next_s, 1000 + 3 * HOUR);
    in.pipe_col = 90;                   /* at the gate: not below it */
    sh_pulse_step(&p, &in, 2000);
    CHECK(!p.watch);

    /* entry by the pipe (pump stopped): a pulse at once */
    in.pipe_col = 89;
    sh_pulse_step(&p, &in, 2000);
    CHECK(p.watch);
    CHECK(p.due);
    CHECK_EQ(p.next_s, 2000);
    /* the pulse: the liquid shows 12 °C (margin 8 K) — 60 min, though the pipe is above the gate now */
    in.pump_on = true;
    in.fresh = true;
    in.rel_col = 120;
    in.rel_s = 2030;
    in.pipe_col = 120;
    sh_pulse_step(&p, &in, 2030);
    CHECK(p.watch);
    CHECK(!p.due);
    in.pump_on = false;
    in.fresh = false;
    sh_pulse_step(&p, &in, 2031);
    CHECK(!p.due);
    CHECK_EQ(p.next_s, 2030 + 60 * MIN);
    sh_pulse_step(&p, &in, 2030 + 60 * MIN - 1);
    CHECK(!p.due);
    sh_pulse_step(&p, &in, 2030 + 60 * MIN);
    CHECK(p.due);
    /* margin above 10 K: 120 min (and not the night pulse of 3 h) */
    in.rel_col = 141;
    sh_pulse_step(&p, &in, 2030 + 120 * MIN - 1);
    CHECK(!p.due);
    sh_pulse_step(&p, &in, 2030 + 120 * MIN);
    CHECK(p.due);
    CHECK_EQ(p.next_s, 2030 + 120 * MIN);
    /* margin up to 5 K: 30 min; exactly 10 K: 60 min */
    CHECK_EQ(sh_pulse_frost_s(50), 30 * MIN);
    CHECK_EQ(sh_pulse_frost_s(51), 60 * MIN);
    CHECK_EQ(sh_pulse_frost_s(100), 60 * MIN);
    CHECK_EQ(sh_pulse_frost_s(101), 120 * MIN);
    CHECK_EQ(sh_pulse_frost_s(-20), 30 * MIN);
    /* the early pulse: the pipe falls 3 K below the last pulse */
    in.rel_col = 141;
    in.pipe_col = 112;
    sh_pulse_step(&p, &in, 2100);
    CHECK(!p.due);
    in.pipe_col = 111;
    sh_pulse_step(&p, &in, 2100);
    CHECK(p.due);
    /* the cooling may ask for more within the watch */
    in.pipe_col = 141;
    in.cool = true;
    sh_pulse_step(&p, &in, 2030 + 30 * MIN);
    CHECK(p.due);
    in.cool = false;
    /* the end: the window opens (the sunrise) */
    in.lt.min = HM(8, 0);
    sh_pulse_step(&p, &in, 2100);
    CHECK(!p.watch);
    /* the next night starts at the gate again */
    in.lt.min = HM(22, 0);
    in.pipe_col = 120;
    sh_pulse_step(&p, &in, 2100);
    CHECK(!p.watch);

    /* entry by a reliable value below the gate (the night pulse): that pulse counts, then by margin */
    p = (sh_pulse_t){ 0 };
    in = pin(HM(1, 0));
    in.pipe_col = 150;
    in.pump_on = true;
    in.fresh = true;
    in.rel_col = 80;                    /* margin 4 K */
    in.rel_s = 5000;
    sh_pulse_step(&p, &in, 5000);
    CHECK(p.watch);
    in.pump_on = false;
    in.fresh = false;
    sh_pulse_step(&p, &in, 5001);
    CHECK(!p.due);
    CHECK_EQ(p.next_s, 5000 + 30 * MIN);
    /* frost protection off: no watch */
    p = (sh_pulse_t){ 0 };
    in.frost_on = false;
    in.pipe_col = 50;
    sh_pulse_step(&p, &in, 5001);
    CHECK(!p.watch);
    /* in the window: no watch (the day pulses) */
    in.frost_on = true;
    in.lt.min = HM(12, 0);
    sh_pulse_step(&p, &in, 5001);
    CHECK(!p.watch);
}

/* The night cooling: the pulses of 30 min only with a cause by the last values (sh_cool_t.pulse). */
static bool cool_pulse(uint16_t min, int16_t tank, bool e4, bool vacation, bool night)
{
    sh_cool_t c = { 0 };
    const sh_cool_in_t in = {
        .reliable = true, .min = min, .t_col = 150, .t_tank = tank, .tank_max = 700, .target = 663,
        .tmax_on = true, .e4 = e4, .vacation = vacation, .night = night,
    };
    sh_cool_step(&c, &in, 0);
    return c.pulse;
}

static void test_cool(void)
{
    CHECK(cool_pulse(HM(4, 0), 700, false, false, true));       /* a): the tank at Tmax, from 04:00 */
    CHECK(!cool_pulse(HM(3, 59), 700, false, false, true));
    CHECK(!cool_pulse(HM(4, 30), 690, false, false, true));     /* below Tmax: the night pulse */
    CHECK(!cool_pulse(HM(7, 0), 750, false, false, true));      /* the window is over */
    CHECK(cool_pulse(HM(3, 0), 680, true, false, true));        /* b): after an E4, from 03:00 */
    CHECK(!cool_pulse(HM(3, 0), 660, true, false, true));       /* at the target */
    CHECK(cool_pulse(HM(23, 0), 400, false, true, true));       /* the vacation: the whole night */
    CHECK(!cool_pulse(HM(12, 0), 400, false, true, false));

    /* the session ended (target reached): no more 30-min pulses this night */
    sh_cool_t c = { 0 };
    sh_cool_in_t in = {
        .reliable = true, .min = HM(4, 0), .t_col = 150, .t_tank = 720, .tank_max = 700, .target = 663,
        .tmax_on = true,
    };
    sh_cool_step(&c, &in, 0);
    CHECK(c.pulse);
    in.t_tank = 660;
    sh_cool_step(&c, &in, 0);
    CHECK(!c.pulse);
    in.t_tank = 720;                    /* the heater warms it again: one session a night */
    sh_cool_step(&c, &in, 0);
    CHECK(!c.pulse);
}

/* ---- the pulse, the reliable values, the decisions (3.2 - 3.4) ----------------------------- */

static void step(sh_pump_ctl_t *c, const sh_pump_params_t *p, int16_t col, int16_t tank, uint32_t t)
{
    const sh_pump_temps_t tt = { .t_col = col, .t_tank = tank };
    sh_pump_ctl_step(c, p, &tt, t);
}

/* A pulse from `t0`, with the pipes at `col` / `tank` during it; returns after its end (t0 + 30). */
static void pulse(sh_pump_ctl_t *c, sh_pump_params_t *p, int16_t col, int16_t tank, uint32_t t0)
{
    p->pulse_due = true;
    step(c, p, 100, 100, t0);            /* the pipes before: nothing reliable */
    p->pulse_due = false;
    for (uint32_t t = t0 + 1; t <= t0 + SH_PULSE_RUN_S; t++) {
        step(c, p, col, tank, t);
    }
}

static void test_reliable(void)
{
    sh_pump_params_t p = PP;
    sh_pump_ctl_t c = { 0 };
    /* nothing yet: the pump waits; the pulse is due */
    step(&c, &p, 300, 400, 1000);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_PULSE_WAIT);
    CHECK(!c.rel.any);
    p.pulse_due = true;
    step(&c, &p, 300, 400, 1001);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_PULSE);
    CHECK(c.events & SH_PUMP_EVT_PULSE_START);
    p.pulse_due = false;
    /* 29 s: not reliable yet, the pulse goes on */
    step(&c, &p, 700, 400, 1030);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_PULSE);
    CHECK(!c.rel.any);
    /* 30 s: reliable, taken at the pulse; difference 30 K -> the pump goes on */
    step(&c, &p, 700, 400, 1031);
    CHECK(c.rel.any);
    CHECK(c.rel.fresh);
    CHECK(c.rel.by_pulse);
    CHECK_EQ(c.rel.col, 700);
    CHECK_EQ(c.rel.tank, 400);
    CHECK_EQ(c.rel.at_s, 1031);
    CHECK(c.events & SH_PUMP_EVT_PULSE_END);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    /* the run goes on: the values follow the pipes */
    step(&c, &p, 650, 420, 1200);
    CHECK_EQ(c.rel.col, 650);
    CHECK_EQ(c.rel.at_s, 1200);
    CHECK(!c.rel.by_pulse);
    /* the difference falls to 4 K: stop; then waiting, the values stay those of the last run */
    step(&c, &p, 460, 420, 1300);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    step(&c, &p, 900, 300, 1301);       /* the pipe with the pump stopped: not taken */
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_PULSE_WAIT);
    CHECK_EQ(c.rel.col, 460);
    CHECK_EQ(c.rel.tank, 420);
    CHECK_EQ(c.rel.at_s, 1300);
    CHECK(!c.rel.fresh);
    /* a faulty sensor drops its value */
    const sh_pump_temps_t tf = { .t_col = 900, .t_tank = 300, .tank_fault = true };
    sh_pump_ctl_step(&c, &p, &tf, 1302);
    CHECK_EQ(c.rel.tank, SH_TEMP_INVALID);
    CHECK_EQ(c.rel.col, 460);
    /* the mounting changes: the values start over */
    p.pulse = false;
    step(&c, &p, 300, 400, 1303);
    p.pulse = true;
    step(&c, &p, 300, 400, 1304);
    CHECK(!c.rel.any);
    CHECK_EQ(c.rel.col, SH_TEMP_INVALID);
}

static void test_after_pulse(void)
{
    sh_pump_params_t p = PP;
    /* the difference below the switch-on difference: stop at once until the next pulse */
    sh_pump_ctl_t c = { 0 };
    pulse(&c, &p, 450, 400, 1000);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    step(&c, &p, 450, 400, 1031);
    CHECK_EQ(c.reason, SH_PUMP_REASON_PULSE_WAIT);
    /* exactly the switch-on difference: the pump goes on */
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 460, 400, 1000);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    /* ... and may stop 30 s later (the minimum run time of the mode) */
    step(&c, &p, 440, 400, 1059);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_MIN_RUN);
    step(&c, &p, 440, 400, 1060);
    CHECK(!c.on);
    /* the minimum collector temperature by the reliable value */
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 240, 150, 1000);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_COL_MIN);
    /* Tmax by the reliable value */
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 900, 700, 1000);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_TANK_MAX);
    /* the collector protection: the pump stays on as a protection, also above Tmax */
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 1100, 720, 1000);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_COL_PROTECT);
    step(&c, &p, 1081, 730, 1100);
    CHECK(c.on);
    step(&c, &p, 1079, 730, 1101);
    CHECK(!c.on);
    /* the frost protection: a pulse below the threshold -> as before, until the threshold + 2 K */
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 30, 400, 1000);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_FROST);
    step(&c, &p, 59, 390, 1100);
    CHECK(c.on);
    step(&c, &p, 60, 390, 1101);
    CHECK(!c.on);
    /* the night cooling: the collector colder by 6 K -> on, off below 4 K */
    p.cool = true;
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 600, 720, 1000);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_COOL);
    step(&c, &p, 670, 720, 1100);
    CHECK(c.on);
    step(&c, &p, 679, 720, 1101);
    CHECK(c.on);
    step(&c, &p, 680, 720, 1102);
    CHECK(!c.on);
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 670, 720, 1000);      /* 5 K: no start */
    CHECK(!c.on);
    p.cool = false;
    /* the vacation: no charging after the pulse */
    p.charge_off = true;
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 700, 400, 1000);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_VACATION);
    p.charge_off = false;
    /* the boiling point by the reliable value: off; the next pulse still runs its 30 s */
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 1280, 700, 1000);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_COL_BOILING);
    CHECK(c.norm.col_boiling);
    pulse(&c, &p, 600, 500, 5000);
    CHECK(!c.norm.col_boiling);
    CHECK(c.on);                        /* 10 K: charging again */
}

static void test_tank_fault(void)
{
    /* the tank sensor faulty: by the collector, with the pulses (>= 60 °C) */
    sh_pump_params_t p = PP;
    sh_pump_ctl_t c = { 0 };
    const sh_pump_temps_t t0 = { .t_col = 500, .t_tank = 400, .tank_fault = true };
    p.pulse_due = true;
    sh_pump_ctl_step(&c, &p, &t0, 1000);
    CHECK_EQ(c.reason, SH_PUMP_REASON_PULSE);
    p.pulse_due = false;
    const sh_pump_temps_t t1 = { .t_col = 650, .t_tank = 400, .tank_fault = true };
    for (uint32_t t = 1001; t <= 1030; t++) {
        sh_pump_ctl_step(&c, &p, &t1, t);
    }
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_TF_RUN);
    CHECK_EQ(c.rel.col, 650);
    CHECK_EQ(c.rel.tank, SH_TEMP_INVALID);
    /* below 60 °C at the next pulse: stays off */
    const sh_pump_temps_t t2 = { .t_col = 550, .t_tank = 400, .tank_fault = true };
    sh_pump_ctl_step(&c, &p, &t2, 1100);
    CHECK(!c.on);
    sh_pump_ctl_step(&c, &p, &t2, 1101);
    CHECK_EQ(c.reason, SH_PUMP_REASON_PULSE_WAIT);
    /* the collector sensor faulty: the probes as before, no pulses */
    c = (sh_pump_ctl_t){ 0 };
    const sh_pump_temps_t t3 = { .t_col = 500, .t_tank = 400, .col_fault = true };
    p.pulse_due = true;
    sh_pump_ctl_step(&c, &p, &t3, 1000);
    CHECK_EQ(c.reason, SH_PUMP_REASON_CF_PROBE);
    CHECK(!c.pulse);
}

static void test_test_replaces(void)
{
    /* the pump test replaces a pulse; after it (values current) the mode decides */
    sh_pump_params_t p = PP;
    sh_pump_ctl_t c = { 0 };
    p.pulse_due = true;
    step(&c, &p, 300, 400, 1000);
    CHECK(c.pulse);
    p.pulse_due = false;
    sh_pump_ctl_plus(&c, 60, 1010);
    CHECK(!c.pulse);
    for (uint32_t t = 1010; t < 1070; t++) {
        step(&c, &p, 700, 400, t);
        CHECK_EQ(c.reason, SH_PUMP_REASON_TEST);
    }
    step(&c, &p, 700, 400, 1070);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    /* the anti-seize run is not needed: a pulse comes at least every 3 h — but it still works */
}

/* The sleeves: unchanged — no pulse, no waiting, the sensors as they are. */
static void test_sleeves(void)
{
    sh_pump_params_t p = PP;
    p.pulse = false;
    p.pulse_due = true;
    p.min_run_s = 180;
    sh_pump_ctl_t c = { 0 };
    step(&c, &p, 300, 400, 1000);
    CHECK(!c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    CHECK(!c.pulse);
    step(&c, &p, 700, 400, 1001);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    step(&c, &p, 430, 400, 1100);
    CHECK(c.on);
    CHECK_EQ(c.reason, SH_PUMP_REASON_MIN_RUN);
    step(&c, &p, 430, 400, 1181);
    CHECK(!c.on);
    step(&c, &p, 430, 400, 1182);
    CHECK_EQ(c.reason, SH_PUMP_REASON_DIFF);
    CHECK(!c.rel.any);
}

/* ---- the heater, the leak, E8, E6 on the pipes (3.4) --------------------------------------- */

static void test_heater_pipe(void)
{
    /* the heater sign: the tank pipe with the pump stopped, the same threshold (1.0 K for 2 kW, 150 l) */
    sh_pump_params_t p = PP;
    p.heater_rise = sh_heater_rise(20, 150);
    sh_pump_ctl_t c = { 0 };
    pulse(&c, &p, 400, 400, 1000);
    CHECK(!c.on);
    for (int i = 0; i <= 10; i++) {
        step(&c, &p, 300, (int16_t)(400 + i), 1100 + (uint32_t)i * 60);
    }
    CHECK(sh_pump_ctl_heater(&c));      /* the pipe rose 1.0 K in 10 min */
    CHECK_EQ(c.rel.tank, 400);          /* the reliable value is not the pipe */
    c = (sh_pump_ctl_t){ 0 };
    pulse(&c, &p, 400, 400, 1000);
    for (int i = 0; i <= 10; i++) {
        step(&c, &p, 300, (int16_t)(400 + i * 9 / 10), 1100 + (uint32_t)i * 60);
    }
    CHECK(!sh_pump_ctl_heater(&c));     /* 0.9 K */

    /* the heater leak: not applied (sh_control gives it no heater) — a long charging run outside the
     * daylight brings no check */
    sh_leak_t l = { 0 };
    const sh_leak_in_t lin = { .heater = false, .reliable = true, .min = HM(18, 0), .rise = -1, .set = -1,
                               .t_col = 500, .pump_on = true, .charging = true };
    for (uint32_t t = 0; t < 4 * HOUR; t += 60) {
        sh_leak_step(&l, &lin, t);
        CHECK_EQ(l.phase, SH_LEAK_IDLE);
    }
}

static void test_e8_pipe(void)
{
    /* E8 on the pipes: at night, the pump stopped 1 h — the collector pipe rises 2.5 K, the tank pipe
     * falls 1.5 K: the sign, as with the sleeves (threshold 1.0 K/h) */
    sh_e8_t e = { 0 };
    sh_e8_learned_reset(&e.l);
    sh_e8_in_t in = { .night = true, .threshold = 10 };
    uint8_t evt = 0;
    for (int i = 0; i <= 60; i++) {
        in.t_col = (int16_t)(100 + i * 25 / 60);
        in.t_tank = (int16_t)(500 - i * 15 / 60);
        sh_e8_step(&e, &in, 1000 + (uint32_t)i * 60);
        evt |= e.events;
    }
    CHECK(evt & SH_E8_EVT_SIGN);
    /* a pulse within the hour (the pump runs) starts the hour again: the night pulses come every 3 h */
    sh_e8_t f = { 0 };
    sh_e8_learned_reset(&f.l);
    evt = 0;
    for (int i = 0; i <= 60; i++) {
        in.t_col = (int16_t)(100 + i * 25 / 60);
        in.t_tank = (int16_t)(500 - i * 15 / 60);
        in.pump_on = i == 30;
        sh_e8_step(&f, &in, 1000 + (uint32_t)i * 60);
        evt |= f.events;
    }
    CHECK(!(evt & SH_E8_EVT_SIGN));
}

static void test_e6(void)
{
    /* E6 only after 10 min of continuous run, not by a pulse */
    static const sh_alarm_params_t A = {
        .e6_time_s = 600, .e6_col_rise = 50, .e6_tank_rise = 5, .e6_diff = 300, .e6_clear_diff = 150,
    };
    sh_alarm_state_t st = { 0 };
    sh_alarm_in_t in = { .t_col = 800, .t_tank = 400, .pump_on = true, .reason = SH_PUMP_REASON_PULSE };
    for (uint32_t t = 0; t < 30; t++) {
        sh_alarm_step(&st, &A, &in, t);
        CHECK(!st.run);
    }
    in.reason = SH_PUMP_REASON_DIFF;    /* the run after the pulse: counts from now */
    sh_alarm_step(&st, &A, &in, 30);
    CHECK(st.run);
    CHECK_EQ(st.next_sample_s, 30 + 600);
}

static void test_day(void)
{
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_PULSE), SH_RUN_OTHER);
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_PULSE_WAIT), -1);
    CHECK_EQ(strcmp(sh_pump_reason_name(SH_PUMP_REASON_PULSE), "pulse"), 0);
    CHECK_EQ(strcmp(sh_pump_reason_name(SH_PUMP_REASON_PULSE_WAIT), "waiting for the next pulse"), 0);
}

int main(void)
{
    test_installer();
    test_window();
    test_sun_start();
    test_day_intervals();
    test_night();
    test_hot();
    test_frost();
    test_cool();
    test_reliable();
    test_after_pulse();
    test_tank_fault();
    test_test_replaces();
    test_sleeves();
    test_heater_pipe();
    test_e8_pipe();
    test_e6();
    test_day();
    return test_summary("test_pulse");
}
