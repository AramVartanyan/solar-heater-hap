/* Host test of group D2 (doc/tasks/step13-pulse-mode.md): the pulse mode of sensors on the pipes —
 * the installer data, the window of the pulses, the intervals of the day, the collector protection,
 * no pulses after an overheat and their return, the night pulse, the night cooling and the vacation,
 * the frost watch, the reliable values and when they were measured (sh_pulse), the pump above them
 * (sh_pump_ctl, sh_pump_logic), sunrise / sunset by the pipes (sh_sun), no heater leak checks, the
 * heater sign and E8 by the pipes (sh_heater, sh_e8), E6 only in a long run (sh_alarm); the mode
 * with sleeves unchanged.
 * The rig below does one second as sh_control does it. Temperatures are tenths of °C, times seconds;
 * local time in minutes of the day. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_pulse.h"
#include "sh_pump_ctl.h"
#include "sh_installer.h"
#include "sh_sun.h"
#include "sh_cool.h"
#include "sh_heater.h"
#include "sh_e8.h"
#include "sh_alarm.h"
#include "sh_day.h"

#define HM(h, m)    ((uint16_t)((h) * 60 + (m)))
#define NONE        SH_TEMP_INVALID

/* The defaults: differences 6 / 4 K, minimum times 3 / 2 min, Tmax 70 °C, C.Lo 25 °C, collector
 * protection 110 °C (ceiling 115 °C), boiling point 127.7 °C, frost threshold 4 °C. */
static const sh_pump_params_t P = {
    .diff_on = 60, .diff_off = 40, .min_run_s = 180, .min_off_s = 120, .tank_max = 700, .col_min = 250,
    .col_protect = 1100, .col_protect_cap = 1150, .col_boiling = 1277, .frost_on = true, .frost = 40,
    .heater_rise = 10,
};

typedef struct {
    /* the installation */
    bool pipes;
    int16_t col, tank;          /* what the sensors measure now */
    bool col_fault, tank_fault;
    bool clock, reliable;       /* the clock is set / reliable */
    uint16_t min0;              /* the local minute at 0 s */
    int rise, set;              /* learned averages, -1: none */
    bool vacation, e4;
    int16_t target;             /* of the night cooling */
    int16_t power;              /* heater, 0.1 kW */
    sh_pump_params_t p;
    /* the state of the control */
    uint32_t now;
    sh_pulse_t pulse;
    sh_sun_t sun;
    sh_cool_t cool;
    sh_heater_t heater;
    sh_leak_t leak;
    sh_e8_t e8;
    sh_pump_ctl_t pump;
    sh_alarm_state_t alarm;
    /* the last step */
    int16_t show_col, show_tank;
    uint8_t sun_evt;
    uint8_t alarms;
    uint32_t starts;            /* how many times the pump went on */
} rig_t;

static void rig_init(rig_t *r, bool pipes, int16_t col, int16_t tank)
{
    memset(r, 0, sizeof(*r));
    r->pipes = pipes;
    r->col = col;
    r->tank = tank;
    r->rise = r->set = -1;
    r->target = 663;
    r->power = 20;
    r->p = P;
    sh_sun_days_reset(&r->sun.days);
    sh_e8_learned_reset(&r->e8.l);
}

/* Learned sunrise and sunset (the averages of sh_sun). */
static void learn(rig_t *r, int rise, int set)
{
    r->rise = rise;
    r->set = set;
    for (int i = 0; i < SH_SUN_DAYS; i++) {
        r->sun.days.rise[i] = (uint16_t)rise;
        r->sun.days.set[i] = (uint16_t)set;
    }
}

static uint16_t rig_min(const rig_t *r)
{
    return (uint16_t)((r->min0 + r->now / 60) % 1440);
}

static const sh_alarm_params_t ALARM = {
    .e6_time_s = 600, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200, .e6_clear_diff = 100,
};

/* One second, in the order of sh_control. */
static void step(rig_t *r)
{
    const uint32_t now = r->now;
    const sh_local_t lt = { .set = r->clock || r->reliable, .day = (uint16_t)(1000 + (r->min0 + now / 60) / 1440),
                            .min = rig_min(r) };
    const int16_t raw_col = r->col_fault ? NONE : r->col;
    const int16_t raw_tank = r->tank_fault ? NONE : r->tank;
    const bool was_on = r->pump.on;

    sh_pulse_in_t pin = {
        .pipes = r->pipes, .t_col = raw_col, .t_tank = raw_tank, .pump_on = r->pump.on,
        .run_s = r->pump.on ? now - r->pump.switched_s : 0, .boiling = r->p.col_boiling,
    };
    sh_pulse_measure(&r->pulse, &pin, now);
    r->show_col = r->pipes ? r->pulse.col : r->col;
    r->show_tank = r->pipes ? r->pulse.tank : r->tank;
    const int16_t t_col = r->col_fault ? NONE : r->show_col;
    const int16_t t_tank = r->tank_fault ? NONE : r->show_tank;

    r->sun_evt = sh_sun_step_flow(&r->sun, raw_col, raw_tank, r->pump.on, r->pulse.fresh, &lt, r->reliable, now);

    sh_pump_params_t p = r->p;
    p.charge_off = r->vacation;
    if (r->pipes) {
        p.pulse = true;
        p.min_run_s = SH_PULSE_S;
    }
    const sh_cool_in_t cin = {
        .reliable = r->reliable, .min = lt.min, .t_col = t_col, .t_tank = t_tank, .tank_max = p.tank_max,
        .target = r->target, .tmax_on = true, .vacation = r->vacation, .e4 = r->e4,
        .night = sh_sun_night(&r->sun, &lt, r->reliable), .dark = r->sun.dark, .sunset_s = r->sun.sunset_s,
        .pump_cool = r->pump.on && r->pump.reason == SH_PUMP_REASON_COOL,
    };
    sh_cool_step(&r->cool, &cin, now);
    p.cool = r->cool.trigger != SH_COOL_NONE;

    const bool heater_on = sh_pump_ctl_heater(&r->pump);
    sh_heater_step(&r->heater, r->power, heater_on);
    const sh_leak_in_t lin = {
        .heater = !r->pipes && sh_heater_present(&r->heater, r->power), .reliable = r->reliable, .min = lt.min,
        .rise = r->rise, .set = r->set, .t_col = t_col, .pump_on = r->pump.on,
        .charging = r->pump.on && r->pump.mode == SH_PUMP_MODE_NORMAL &&
                    (r->pump.reason == SH_PUMP_REASON_DIFF || r->pump.reason == SH_PUMP_REASON_MIN_RUN),
        .sunrise = (r->sun_evt & SH_SUN_EVT_RISE) != 0,
    };
    sh_leak_step(&r->leak, &lin, now);
    p.leak_check = r->leak.phase == SH_LEAK_CHECK;
    p.leak_stop = r->leak.phase == SH_LEAK_STOP;
    p.leak_resume = r->leak.resume;

    const sh_e8_in_t ein = {
        .night = sh_sun_night(&r->sun, &lt, r->reliable), .t_col = raw_col, .t_tank = raw_tank,
        .pump_on = r->pump.on, .heater = heater_on, .threshold = 10,
    };
    sh_e8_step(&r->e8, &ein, now);

    pin.reliable = r->reliable;
    pin.min = lt.min;
    pin.rise = r->rise;
    pin.set = r->set;
    pin.sunrise = (r->sun_evt & SH_SUN_EVT_RISE) != 0;
    pin.sunset = (r->sun_evt & SH_SUN_EVT_SET) != 0;
    pin.cooling = p.cool;
    pin.frost_on = p.frost_on;
    pin.frost = p.frost;
    p.pulse_due = sh_pulse_plan(&r->pulse, &pin, now);
    p.pulse_fresh = r->pulse.fresh;

    const sh_pump_temps_t temps = {
        .t_col = r->show_col, .t_tank = r->show_tank, .col_fault = r->col_fault, .tank_fault = r->tank_fault,
        .pipe_tank = r->tank,
    };
    sh_pump_ctl_step(&r->pump, &p, &temps, now);
    r->pump.events = 0;
    if (r->pump.on && !was_on) {
        r->starts++;
    }
    const sh_alarm_in_t ain = {
        .t_col = r->show_col, .t_tank = r->show_tank, .col_fault = r->col_fault, .tank_fault = r->tank_fault,
        .pump_on = r->pump.on, .reason = r->pump.reason, .col_boiling = r->pump.norm.col_boiling,
        .tank_ceiling = r->pump.norm.tank_ceiling, .e8 = r->e8.l.active,
    };
    r->alarms = sh_alarm_step(&r->alarm, &ALARM, &ain, now);
    r->now++;
}

static void run(rig_t *r, uint32_t s)
{
    while (s--) {
        step(r);
    }
}

/* Steps until the pump goes on (true) or `max_s` are over; `r->now - 1` is then the second of the start. */
static bool until_on(rig_t *r, uint32_t max_s)
{
    while (max_s--) {
        step(r);
        if (r->pump.on) {
            return true;
        }
    }
    return false;
}

static bool until_off(rig_t *r, uint32_t max_s)
{
    while (max_s--) {
        step(r);
        if (!r->pump.on) {
            return true;
        }
    }
    return false;
}

/* The mode starts (one step) and reliable values are given as if a pulse had just measured them. */
static void seed(rig_t *r, int16_t col, int16_t tank)
{
    step(r);
    r->pulse.col = col;
    r->pulse.tank = tank;
    r->pulse.have = true;
    r->pulse.meas_s = r->now;
}

/* One whole pulse that ends with a stop: from the start of the pump to the step after it. */
static void pulse_out(rig_t *r)
{
    CHECK(r->pump.on && r->pump.reason == SH_PUMP_REASON_PULSE);
    CHECK(until_off(r, SH_PULSE_S + 2));
    step(r);
    CHECK_EQ(r->pump.reason, SH_PUMP_REASON_PULSE_WAIT);
}

/* ---- installer data (3.1) ------------------------------------------------------------------- */

static void test_installer(void)
{
    sh_installer_t d = { .v = {
        [SH_INST_FLUID] = SH_FLUID_WATER, [SH_INST_PRESSURE] = 15, [SH_INST_COL_MAX] = 120,
        [SH_INST_COL_TYPE] = SH_COL_FLAT, [SH_INST_COL_AREA] = 20, [SH_INST_TANK_VOL] = 150,
        [SH_INST_HEATER] = 20,
    } };
    CHECK_EQ(d.v[SH_INST_MOUNT], SH_MOUNT_SLEEVES);             /* in sleeves by default */
    CHECK(sh_installer_ok(&d));
    CHECK_EQ(strcmp(sh_inst_info[SH_INST_MOUNT].key, "mount"), 0);
    d.v[SH_INST_MOUNT] = SH_MOUNT_PIPES;
    CHECK(sh_installer_ok(&d));
    d.v[SH_INST_MOUNT] = 2;
    CHECK(!sh_installer_ok(&d));
    d.v[SH_INST_MOUNT] = -1;
    CHECK(!sh_installer_ok(&d));
    sh_installer_t def = d;
    def.v[SH_INST_MOUNT] = SH_MOUNT_SLEEVES;
    CHECK_EQ(sh_installer_sanitize(&d, &def), 1u << SH_INST_MOUNT);
    CHECK_EQ(d.v[SH_INST_MOUNT], SH_MOUNT_SLEEVES);
    /* the mounting changes none of the computed limits */
    sh_limits_t a, b;
    sh_limits_calc(&d, &a);
    d.v[SH_INST_MOUNT] = SH_MOUNT_PIPES;
    sh_limits_calc(&d, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

/* ---- the window and the intervals (3.2) ------------------------------------------------------ */

static void test_window(void)
{
    /* reliable clock, nothing learned: 08:00 - 18:00, to the minute */
    CHECK(!sh_pulse_window(true, HM(7, 59), -1, -1, false));
    CHECK(sh_pulse_window(true, HM(8, 0), -1, -1, false));
    CHECK(sh_pulse_window(true, HM(17, 59), -1, -1, false));
    CHECK(!sh_pulse_window(true, HM(18, 0), -1, -1, false));
    CHECK(!sh_pulse_window(true, HM(12, 0) + 720, -1, -1, true));       /* midnight; the detected day is not used */
    /* only one of the two learned: still the fixed window */
    CHECK(!sh_pulse_window(true, HM(7, 0), HM(5, 50), -1, false));
    CHECK(sh_pulse_window(true, HM(17, 0), -1, HM(16, 45), false));
    /* learned: from the sunrise to the sunset (June 05:50 - 21:10, December 07:50 - 16:45) */
    CHECK(!sh_pulse_window(true, HM(5, 49), HM(5, 50), HM(21, 10), false));
    CHECK(sh_pulse_window(true, HM(5, 50), HM(5, 50), HM(21, 10), false));
    CHECK(sh_pulse_window(true, HM(21, 9), HM(5, 50), HM(21, 10), false));
    CHECK(!sh_pulse_window(true, HM(21, 10), HM(5, 50), HM(21, 10), false));
    CHECK(!sh_pulse_window(true, HM(7, 49), HM(7, 50), HM(16, 45), false));
    CHECK(sh_pulse_window(true, HM(7, 50), HM(7, 50), HM(16, 45), false));
    CHECK(sh_pulse_window(true, HM(16, 44), HM(7, 50), HM(16, 45), false));
    CHECK(!sh_pulse_window(true, HM(16, 45), HM(7, 50), HM(16, 45), false));
    /* without a reliable clock: from the detected sunrise to the detected sunset, whatever the hour */
    CHECK(!sh_pulse_window(false, HM(12, 0), HM(5, 50), HM(21, 10), false));
    CHECK(sh_pulse_window(false, HM(2, 0), HM(5, 50), HM(21, 10), true));
}

static void test_intervals(void)
{
    CHECK_EQ(sh_pulse_day_interval(NONE), 1800);        /* no pulse yet */
    CHECK_EQ(sh_pulse_day_interval(-100), 1800);
    CHECK_EQ(sh_pulse_day_interval(499), 1800);         /* below 50 °C: 30 min */
    CHECK_EQ(sh_pulse_day_interval(500), 600);          /* 50 - 80 °C: 10 min */
    CHECK_EQ(sh_pulse_day_interval(800), 600);
    CHECK_EQ(sh_pulse_day_interval(801), 180);          /* above 80 °C: 3 min */
    CHECK_EQ(sh_pulse_day_interval(1250), 180);
    /* the frost pulses by the reserve */
    CHECK_EQ(sh_pulse_frost_interval(-30), 1800);
    CHECK_EQ(sh_pulse_frost_interval(50), 1800);        /* up to 5 K: 30 min */
    CHECK_EQ(sh_pulse_frost_interval(51), 3600);        /* 5 - 10 K: 60 min */
    CHECK_EQ(sh_pulse_frost_interval(100), 3600);
    CHECK_EQ(sh_pulse_frost_interval(101), 7200);       /* above 10 K: 120 min */
    CHECK(sh_pulse_fresh(true, 30) && !sh_pulse_fresh(true, 29) && !sh_pulse_fresh(false, 100));
}

/* A pulse, the decision after it, the reliable values. */
static void test_pulse_cycle(void)
{
    rig_t r;
    rig_init(&r, true, 300, 400);
    r.reliable = true;
    r.min0 = HM(9, 0);
    /* nothing measured yet: nothing is shown, the pipes are not shown either; the timers count from
     * the start of the mode — the first pulse of the day after 30 min */
    step(&r);
    CHECK(!r.pump.on);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_PULSE_WAIT);
    CHECK_EQ(r.show_col, NONE);
    CHECK_EQ(r.show_tank, NONE);
    CHECK(!r.pulse.have);
    CHECK(r.pulse.next_known);
    CHECK_EQ(r.pulse.next_s, 1800);
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, 1800);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_PULSE);
    /* 30 s: the values are not reliable before */
    run(&r, 29);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_PULSE);
    CHECK_EQ(r.show_col, NONE);
    step(&r);                                           /* 1830 s: measured, the usual rule decides */
    CHECK_EQ(r.show_col, 300);
    CHECK_EQ(r.show_tank, 400);
    CHECK_EQ(r.pulse.meas_s, 1830);
    CHECK(r.pulse.events & SH_PULSE_EVT_MEASURED);
    CHECK(!r.pump.on);                                  /* no difference: off until the next pulse */
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_DIFF);
    step(&r);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_PULSE_WAIT);
    CHECK_EQ(r.pulse.next_s, 1830 + 1800);

    /* between the pulses the pipes may show anything: not shown, and the pump does not start */
    r.col = 900;
    r.tank = 200;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, 1830 + 1800);                   /* 30 min after the last reliable values */
    CHECK_EQ(r.show_col, 300);
    CHECK_EQ(r.show_tank, 400);

    /* the liquid of this pulse: 46 °C against 40 °C — the difference reaches 6 K, the pump goes on */
    r.col = 460;
    r.tank = 400;
    run(&r, 30);
    CHECK(r.pump.on);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_DIFF);
    CHECK_EQ(r.starts, 2);                              /* the relay did not switch in between */
    /* a continuous run: the values follow the liquid, measured "now" */
    r.col = 455;
    r.tank = 405;
    run(&r, 100);
    CHECK(r.pump.on);
    CHECK_EQ(r.show_col, 455);
    CHECK_EQ(r.show_tank, 405);
    CHECK_EQ(r.pulse.meas_s, r.now - 1);
    /* 4 K: off (the pulse was the minimum run time); the next pulse 30 min after the stop */
    r.col = 445;
    step(&r);
    CHECK(!r.pump.on);
    const uint32_t stop = r.now - 1;
    CHECK_EQ(r.show_col, 445);
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, stop + 1800);

    /* 5.9 K at the end of a pulse is not enough; the collector minimum holds too */
    r.col = 459;
    r.tank = 400;
    pulse_out(&r);
    r.col = 240;                                        /* below C.Lo 25 °C */
    r.tank = 100;
    CHECK(until_on(&r, 3600));
    CHECK(until_off(&r, 40));
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_COL_MIN);
    /* Tmax by the reliable values */
    r.col = 900;
    r.tank = 700;
    CHECK(until_on(&r, 3600));
    CHECK(until_off(&r, 40));
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_TANK_MAX);
}

/* The interval follows the collector of the last pulse: 50 / 80 °C. */
static void test_day_intervals(void)
{
    static const struct { int16_t col; uint32_t wait; } T[] = {
        { 499, 1800 }, { 500, 600 }, { 800, 600 }, { 801, 180 }, { 1000, 180 },
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        rig_t r;
        rig_init(&r, true, T[i].col, 990);              /* a tank that takes nothing: no run after the pulse */
        r.tank = (int16_t)(T[i].col - 10);
        r.reliable = true;
        r.min0 = HM(9, 0);
        CHECK(until_on(&r, 3600));
        pulse_out(&r);
        const uint32_t meas = r.pulse.meas_s;
        CHECK(until_on(&r, 3600));
        CHECK_EQ(r.now - 1, meas + T[i].wait);
    }
}

/* At the threshold of the collector protection the pump stays on as the protection. */
static void test_col_protect(void)
{
    rig_t r;
    rig_init(&r, true, 1100, 800);                      /* the tank above Tmax too */
    r.reliable = true;
    r.min0 = HM(12, 0);
    CHECK(until_on(&r, 3600));
    run(&r, 30);
    CHECK(r.pump.on);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_COL_PROTECT);
    CHECK(r.alarms & SH_ALARM_BIT(SH_ALARM_HOT));
    CHECK(!r.pulse.block);                              /* 110 °C is below the boiling point - 10 K (117.7 °C) */
    run(&r, 600);
    CHECK(r.pump.on);
    r.col = 1080;                                       /* not yet below the threshold - 2 K */
    run(&r, 10);
    CHECK(r.pump.on);
    r.col = 1079;
    step(&r);
    CHECK(!r.pump.on);
    const uint32_t stop = r.now - 1;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, stop + 180);                    /* above 80 °C: every 3 min */
    /* one tenth below the threshold: no protection, the tank is at Tmax */
    rig_init(&r, true, 1099, 800);
    r.reliable = true;
    r.min0 = HM(12, 0);
    CHECK(until_on(&r, 3600));
    CHECK(until_off(&r, 40));
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_TANK_MAX);
}

/* An overheat (the tank at its ceiling, so the pump stops) at the local minute `at`; returns the rig
 * right after the stop. */
static void overheat(rig_t *r, int16_t col, uint16_t at, bool reliable, int rise, int set)
{
    rig_init(r, true, col, 960);
    r->reliable = reliable;
    if (rise >= 0) {
        learn(r, rise, set);
    }
    r->now = 4 * 3600;                                  /* (the mode starts 4 h after the boot) */
    r->min0 = (uint16_t)((at + 1440 - 240) % 1440);
    step(r);
    r->pulse.meas_s = 0;                                /* the last values are 4 h old: a pulse is due at once */
    CHECK(until_on(r, 5));
    CHECK(until_off(r, 40));
    CHECK(r->pump.reason == SH_PUMP_REASON_TANK_CEILING || r->pump.reason == SH_PUMP_REASON_COL_BOILING);
    step(r);
}

/* No pulses after the boiling point - 10 K, and their return. */
static void test_overheat(void)
{
    rig_t r;
    /* 117.6 °C: one tenth below — pulses go on every 3 min */
    overheat(&r, 1176, HM(13, 0), false, -1, -1);
    CHECK(!r.pulse.block);
    r.pulse.day = true;                                 /* (the window without a clock) */
    CHECK(until_on(&r, 200));
    CHECK_EQ(r.now - 1, r.pulse.meas_s + 180);

    /* 117.7 °C, no reliable clock: nothing for 5 h, whatever else would ask for a pulse */
    overheat(&r, 1177, HM(13, 0), false, -1, -1);
    CHECK(r.pulse.block);
    CHECK_EQ(r.pulse.block_until, SH_PULSE_UNTIL_TIME);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_PULSE_WAIT);
    CHECK(!r.pulse.next_known);
    const uint32_t hot = r.pulse.block_s;
    CHECK_EQ(hot, r.pulse.meas_s);
    r.pulse.day = true;
    r.col = 20;                                         /* (even a pipe below the frost gate) */
    CHECK(until_on(&r, 6 * 3600));
    CHECK_EQ(r.now - 1, hot + 5 * 3600);
    CHECK(!r.pulse.block);

    /* a reliable clock and a learned sunset at 20:00: the first pulse at 20:00 */
    overheat(&r, 1200, HM(13, 0), true, HM(6, 0), HM(20, 0));
    CHECK(r.pulse.block);
    CHECK_EQ(r.pulse.block_until, SH_PULSE_UNTIL_SUNSET);
    CHECK(until_on(&r, 9 * 3600));
    CHECK_EQ(rig_min(&r), HM(20, 0));
    CHECK_EQ((r.now - 1) % 60, 0);
    /* ... also when that is less than 5 h away */
    overheat(&r, 1200, HM(18, 0), true, HM(6, 0), HM(20, 0));
    CHECK(until_on(&r, 9 * 3600));
    CHECK_EQ(rig_min(&r), HM(20, 0));
    /* the clock is lost meanwhile: 5 h from the hot value */
    overheat(&r, 1200, HM(18, 0), true, HM(6, 0), HM(20, 0));
    r.reliable = false;
    r.clock = true;
    CHECK(until_on(&r, 9 * 3600));
    CHECK_EQ(r.now - 1, r.pulse.block_s + 5 * 3600);

    /* a reliable clock, nothing learned yet: 18:00 (the end of the fixed window), but not earlier
     * than 5 h after the hot pulse */
    overheat(&r, 1200, HM(10, 0), true, -1, -1);
    CHECK_EQ(r.pulse.block_until, SH_PULSE_UNTIL_FIXED);
    CHECK(until_on(&r, 12 * 3600));
    CHECK_EQ(rig_min(&r), HM(18, 0));                   /* overheat at 10:00 -> 18:00 */
    CHECK_EQ((r.now - 1) % 60, 0);
    overheat(&r, 1200, HM(16, 0), true, -1, -1);
    CHECK(until_on(&r, 12 * 3600));
    CHECK_EQ(rig_min(&r), HM(21, 0));                   /* overheat at 16:00 -> 21:00 */
    CHECK_EQ(r.now - 1, r.pulse.block_s + 5 * 3600);

    /* the protection run cooled the collector: the last reliable value is not hot, pulses go on */
    rig_init(&r, true, 1200, 800);
    r.reliable = true;
    r.min0 = HM(13, 0);
    CHECK(until_on(&r, 3600));
    run(&r, 31);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_COL_PROTECT);
    CHECK(r.pulse.block);
    r.col = 1000;
    run(&r, 5);
    CHECK(!r.pulse.block);
    CHECK(!r.pump.on);
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, r.pulse.meas_s + 180);

    /* the boiling point itself in a pulse: E5, and it stays until a later pulse shows less */
    overheat(&r, 1280, HM(13, 0), false, -1, -1);
    CHECK(r.alarms & SH_ALARM_BIT(SH_ALARM_E5));
    r.tank = 600;
    r.col = 500;
    run(&r, 3600);
    CHECK(r.alarms & SH_ALARM_BIT(SH_ALARM_E5));
    CHECK(until_on(&r, 5 * 3600));
    run(&r, 31);
    CHECK(!(r.alarms & SH_ALARM_BIT(SH_ALARM_E5)));
}

/* ---- the night (3.3) ------------------------------------------------------------------------- */

/* Outside the window: a pulse every 3 h after the last reliable values. */
static void test_night_pulse(void)
{
    rig_t r;
    rig_init(&r, true, 150, 450);
    r.reliable = true;
    r.min0 = HM(20, 0);
    step(&r);
    CHECK_EQ(r.pulse.next_s, 3 * 3600);
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, 3 * 3600);                      /* 23:00 */
    pulse_out(&r);
    CHECK_EQ(r.show_col, 150);
    uint32_t meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 3 * 3600);               /* 02:00 */
    pulse_out(&r);
    meas = r.pulse.meas_s;                              /* 02:00:30 */
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 3 * 3600);               /* 05:00:30 */
    pulse_out(&r);
    /* the window opens at 08:00: the pulse of the day comes then (30 min are long over) */
    meas = r.pulse.meas_s;
    CHECK(r.pulse.next_s < meas + 3 * 3600);            /* foreseen: 08:00, before the 3 h */
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(rig_min(&r), HM(8, 0));
    CHECK_EQ((r.now - 1) % 60, 0);
    /* ... and the last pulse of the day is before 18:00 */
    r.col = 300;
    pulse_out(&r);
    uint32_t last = 0;
    while (rig_min(&r) < HM(18, 30)) {
        step(&r);
        if (r.pump.on && r.pump.reason == SH_PUMP_REASON_PULSE && r.now - 1 == r.pump.switched_s) {
            last = r.now - 1;
        }
    }
    CHECK(last != 0);
    CHECK(last + 1800 > r.now - 30 * 60);               /* one of the last half hour before 18:00 */
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, r.pulse.meas_s + 3 * 3600);     /* then every 3 h again */
}

/* Night cooling: a pulse every 30 min only with a trigger. */
static void test_cooling(void)
{
    rig_t r;
    /* a) no trigger: the tank of the last reliable values is below Tmax — only the 3 h pulse */
    rig_init(&r, true, 150, 600);
    r.reliable = true;
    r.min0 = HM(3, 30);
    seed(&r, 150, 600);
    while (rig_min(&r) < HM(6, 29)) {
        step(&r);
        CHECK(!r.pump.on);
    }
    CHECK_EQ(r.cool.trigger, SH_COOL_NONE);
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, 1 + 3 * 3600);                  /* 06:30, 3 h after the values */

    /* a) with the trigger: the tank at Tmax; from 04:00 a pulse every 30 min */
    rig_init(&r, true, 150, 720);
    r.reliable = true;
    r.min0 = HM(3, 50);
    seed(&r, 150, 720);
    r.col = 690;                                        /* the collector 3 K below the tank: nothing to cool with */
    while (rig_min(&r) < HM(4, 0)) {
        step(&r);
        CHECK(!r.pump.on);
    }
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.cool.trigger, SH_COOL_TMAX);
    CHECK_EQ(r.now - 1, 1 + 1800);                      /* 04:20: 30 min after the values, in the window */
    CHECK(until_off(&r, 40));                           /* 5.9 K would not do either; here 3 K */
    step(&r);
    uint32_t meas = r.pulse.meas_s;
    r.col = 150;                                        /* the collector has cooled */
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, meas + 1800);                   /* the next pulse after 30 min */
    run(&r, 31);
    CHECK(r.pump.on);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_COOL);       /* the collector is colder by >= 6 K: cooling */
    r.tank = 700;
    run(&r, 60);
    CHECK(r.pump.on);
    r.col = 660;                                        /* 4 K: off, the next pulse after 30 min */
    step(&r);
    CHECK(!r.pump.on);
    meas = r.pulse.meas_s;
    r.col = 150;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, meas + 1800);
    run(&r, 31);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_COOL);
    r.tank = 663;                                       /* the target: the session is over */
    run(&r, 2);
    CHECK_EQ(r.cool.trigger, SH_COOL_NONE);
    CHECK_EQ(r.cool.end, SH_COOL_END_TARGET);
    CHECK(until_off(&r, 5));
    CHECK(!until_on(&r, 3600));                         /* no more pulses of 30 min this night */

    /* b) after an E4: the tank above the target, from 03:00 */
    rig_init(&r, true, 150, 690);
    r.reliable = true;
    r.min0 = HM(2, 0);
    r.e4 = true;
    seed(&r, 150, 690);
    r.e4 = false;
    step(&r);
    CHECK(until_on(&r, 2 * 3600));
    CHECK_EQ(rig_min(&r), HM(3, 0));
    CHECK_EQ(r.cool.trigger, SH_COOL_E4);
    run(&r, 31);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_COOL);
    /* b) without it: the tank of the last values at the target */
    rig_init(&r, true, 150, 663);
    r.reliable = true;
    r.min0 = HM(2, 0);
    r.e4 = true;
    seed(&r, 150, 663);
    r.e4 = false;
    CHECK(!until_on(&r, 2 * 3600 + 1800));
    CHECK_EQ(r.cool.trigger, SH_COOL_NONE);

    /* without a reliable clock: 6 h after the detected sunset */
    rig_init(&r, true, 500, 720);
    r.pulse.day = true;
    seed(&r, 800, 720);
    r.pulse.day = true;
    r.col = 800;                                        /* the pipe above the tank pipe: day */
    run(&r, 5);
    r.col = 300;                                        /* ... and below it: sunset */
    step(&r);
    CHECK(r.sun_evt & SH_SUN_EVT_SET);
    CHECK(!r.pulse.day);                                /* the window closed */
    const uint32_t sunset = r.now - 1;
    uint32_t n = 0, first = 0;
    while (r.now < sunset + 6 * 3600 + 120) {
        step(&r);
        if (r.pump.on && r.now - 1 == r.pump.switched_s) {
            n++;
            first = first ? first : r.now - 1;
        }
    }
    CHECK_EQ(r.cool.trigger, SH_COOL_TMAX);
    CHECK(n == 2 || n == 3);                            /* the 3 h pulse(s), then the one of the cooling */
    CHECK(first >= sunset + 3 * 3600 - 60);
}

/* The vacation: the whole night a pulse every 30 min; by day no charging. */
static void test_vacation(void)
{
    rig_t r;
    rig_init(&r, true, 200, 500);
    r.reliable = true;
    learn(&r, HM(6, 0), HM(20, 0));
    r.vacation = true;
    r.min0 = HM(22, 0);
    seed(&r, 470, 500);                                 /* the collector 3 K below the tank */
    r.col = 470;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.cool.trigger, SH_COOL_VACATION);
    CHECK_EQ(r.now - 1, 1 + 1800);
    pulse_out(&r);
    uint32_t meas = r.pulse.meas_s;
    r.col = 440;                                        /* 6 K below */
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, meas + 1800);
    run(&r, 31);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_COOL);
    r.col = 461;                                        /* under 4 K: off */
    run(&r, 2);
    CHECK(!r.pump.on);
    meas = r.pulse.meas_s;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, meas + 1800);
    /* by day: the pulses of the window, but no charging */
    rig_init(&r, true, 600, 300);
    r.reliable = true;
    r.vacation = true;
    r.min0 = HM(10, 0);
    CHECK(until_on(&r, 3600));
    CHECK(until_off(&r, 40));
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_VACATION);
}

/* The frost watch: the gate is only the way in. */
static void test_frost(void)
{
    rig_t r;
    /* in by the pipe: at the threshold + 5 K (9.0 °C) nothing, one tenth below a pulse at once */
    rig_init(&r, true, 200, 300);
    r.reliable = true;
    r.min0 = HM(22, 0);
    seed(&r, 200, 300);
    r.col = 90;
    run(&r, 1200);
    CHECK(!r.pump.on && !r.pulse.frost_watch);
    CHECK_EQ(r.starts, 0);
    r.col = 89;
    step(&r);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_PULSE);
    CHECK(r.pulse.frost_watch);
    /* the liquid of the pulse 6.0 °C: reserve 2 K -> the next after 30 min */
    r.col = 60;
    pulse_out(&r);
    CHECK_EQ(r.show_col, 60);
    uint32_t meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 1800);
    /* 9.0 °C: reserve 5.0 K -> still 30 min; the pipe at the gate does not end the watch */
    r.col = 90;
    pulse_out(&r);
    meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 1800);
    /* 9.1 °C: reserve 5.1 K -> 60 min — reached, although the pipe is above the gate */
    r.col = 91;
    pulse_out(&r);
    meas = r.pulse.meas_s;
    CHECK(r.pulse.frost_watch);
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 3600);
    /* 14.0 °C: reserve 10.0 K -> 60 min; 14.1 °C -> 120 min (not the 3 h of the night pulse) */
    r.col = 140;
    pulse_out(&r);
    meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 3600);
    r.col = 141;
    pulse_out(&r);
    meas = r.pulse.meas_s;                              /* about 01:30 */
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 7200);
    /* the early pulse: the pipe with the pump stopped 3 K below the last pulse */
    pulse_out(&r);
    meas = r.pulse.meas_s;
    r.col = 112;                                        /* 2.9 K below 14.1 °C */
    run(&r, 600);
    CHECK(!r.pump.on);
    r.col = 111;
    step(&r);
    CHECK(r.pump.on);
    CHECK(r.now - 1 < meas + 1800);
    /* a pulse below the threshold: the frost protection runs as before, to the threshold + 2 K */
    r.col = 39;
    run(&r, 31);
    CHECK(r.pump.on);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_FROST);
    CHECK(r.alarms & SH_ALARM_BIT(SH_ALARM_FRO));
    r.col = 59;
    run(&r, 300);
    CHECK(r.pump.on);
    r.col = 60;
    step(&r);
    CHECK(!r.pump.on);
    meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 1800);                   /* reserve 2 K */
    /* the watch ends when the window opens (08:00); the pulses are then those of the day */
    pulse_out(&r);
    while (rig_min(&r) != HM(8, 0)) {
        step(&r);
    }
    step(&r);
    CHECK(!r.pulse.frost_watch);

    /* in by a reliable value: the night pulse of 3 h finds 8.9 °C, the pipe never was below the gate */
    rig_init(&r, true, 200, 300);
    r.reliable = true;
    r.min0 = HM(20, 0);
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, 3 * 3600);
    CHECK(!r.pulse.frost_watch);
    r.col = 89;
    pulse_out(&r);
    r.col = 200;                                        /* (the pipe warms again above the gate) */
    step(&r);
    CHECK(r.pulse.frost_watch);
    meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 1800);                   /* reserve 4.9 K: 30 min, no extra pulse at once */
    /* at the gate (9.0 °C) the value does not start the watch */
    rig_init(&r, true, 200, 300);
    r.reliable = true;
    r.min0 = HM(20, 0);
    CHECK(until_on(&r, 4 * 3600));
    r.col = 90;
    pulse_out(&r);
    r.col = 200;
    step(&r);
    CHECK(!r.pulse.frost_watch);
    meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 3 * 3600);

    /* by day the gate starts nothing: the pulses of the window do the watching */
    rig_init(&r, true, 50, 300);
    r.reliable = true;
    r.min0 = HM(10, 0);
    run(&r, 600);
    CHECK(!r.pulse.frost_watch);
    CHECK_EQ(r.starts, 0);
    /* the protection switched off: no frost pulses */
    rig_init(&r, true, 50, 300);
    r.reliable = true;
    r.min0 = HM(22, 0);
    r.p.frost_on = false;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, 3 * 3600);
    /* nothing measured yet and the pipe below the gate: at once */
    rig_init(&r, true, 50, 300);
    r.reliable = true;
    r.min0 = HM(22, 0);
    step(&r);
    CHECK(r.pump.on);
}

/* ---- sunrise / sunset by the pipes, the start without a clock ------------------------------- */

static void test_sun(void)
{
    rig_t r;
    rig_init(&r, true, 100, 300);
    /* no clock, nothing detected: no window — only the 3 h pulse */
    step(&r);
    CHECK(!r.pulse.day);
    CHECK_EQ(r.pulse.next_s, 3 * 3600);
    run(&r, 900);
    /* the collector pipe rises 1.9 K in 10 min with the pump stopped: not yet */
    for (int i = 1; i <= 19; i++) {
        r.col = (int16_t)(100 + i);
        run(&r, 32);
    }
    CHECK(!r.pulse.day);
    CHECK_EQ(r.starts, 0);
    /* 2 K in 10 min: the sunrise — the window opens and the first pulse comes (30 min are over) */
    for (int i = 1; i <= 10 && !r.pulse.day; i++) {
        r.col = (int16_t)(r.col + 2);
        run(&r, 60);
    }
    CHECK(r.pulse.day);
    CHECK_EQ(r.starts, 1);
    CHECK(until_off(&r, 40));
    /* day: pulses every 30 min */
    step(&r);
    uint32_t meas = r.pulse.meas_s;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, meas + 1800);
    /* the collector above the tank in a pulse, the pump runs ... */
    r.col = 450;
    r.tank = 300;
    run(&r, 31);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_DIFF);
    /* ... and falls below it while the pump runs: the sunset by the values of the flow */
    r.col = 340;
    run(&r, 2);
    CHECK(!r.pump.on);
    CHECK(!(r.sun_evt & SH_SUN_EVT_SET) && r.pulse.day);
    CHECK(until_on(&r, 3600));
    r.col = 290;
    run(&r, 29);
    CHECK(r.pulse.day);                                 /* not before the 30 s of the pulse */
    run(&r, 2);
    CHECK(!r.pulse.day);                                /* the window closed */
    CHECK(r.sun.dark);
    meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 3 * 3600);               /* night: every 3 h */

    /* the sunset with the pump stopped: the collector pipe falls below the tank pipe */
    sh_sun_t s;
    memset(&s, 0, sizeof(s));
    sh_sun_days_reset(&s.days);
    const sh_local_t lt = { .set = false };
    CHECK_EQ(sh_sun_step_flow(&s, 400, 300, false, false, &lt, false, 10), 0);
    CHECK_EQ(sh_sun_step_flow(&s, 290, 300, true, false, &lt, false, 11), 0);       /* a pulse, not 30 s yet */
    CHECK_EQ(sh_sun_step_flow(&s, 290, 300, false, false, &lt, false, 12), SH_SUN_EVT_SET);
    /* in sleeves a running pump never gives a sunset */
    memset(&s, 0, sizeof(s));
    sh_sun_days_reset(&s.days);
    CHECK_EQ(sh_sun_step(&s, 400, 300, false, &lt, false, 10), 0);
    CHECK_EQ(sh_sun_step(&s, 290, 300, true, &lt, false, 11), 0);
}

/* ---- the other functions (3.4) --------------------------------------------------------------- */

/* No leak checks of the heater in the pulse mode; with sleeves the same evening has one. */
static void test_no_leak(void)
{
    for (int pipes = 0; pipes <= 1; pipes++) {
        rig_t r;
        rig_init(&r, pipes != 0, 600, 400);
        r.reliable = true;
        r.min0 = HM(17, 0);                             /* outside 09:00 - 16:00, in the window of the pulses */
        bool check = false;
        for (uint32_t i = 0; i < 2 * 3600 - 600; i++) {
            step(&r);
            check = check || r.leak.phase != SH_LEAK_IDLE || r.pump.reason == SH_PUMP_REASON_LEAK_CHECK;
        }
        CHECK_EQ(check, pipes == 0);
        if (pipes) {
            CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_DIFF);   /* charging the whole time */
            CHECK(!r.leak.last);
        }
    }
}

/* The heater sign and E8 follow the pipes with the pump stopped, not the reliable values. */
static void test_heater_e8(void)
{
    rig_t r;
    rig_init(&r, true, 150, 400);
    r.reliable = true;
    learn(&r, HM(6, 0), HM(20, 0));
    r.min0 = HM(21, 0);
    seed(&r, 150, 400);
    /* the tank pipe rises 1.0 K in 10 min (2 kW, 150 l): the sign, the same threshold */
    run(&r, 660);
    CHECK(!sh_pump_ctl_heater(&r.pump));
    for (int i = 1; i <= 10; i++) {
        r.tank = (int16_t)(400 + (i < 10 ? i : 9));
        run(&r, 60);
    }
    CHECK(!sh_pump_ctl_heater(&r.pump));                /* 0.9 K */
    r.tank = 411;
    run(&r, 61);
    CHECK(sh_pump_ctl_heater(&r.pump));
    CHECK_EQ(r.show_tank, 400);                         /* the shown value did not move */
    /* detected with "0 kW" */
    r.power = 0;
    step(&r);
    CHECK(r.heater.detected);

    /* E8: at night with the pump stopped the collector pipe rises 2 K in the hour, the tank pipe
     * falls 1.2 K and the difference shrinks */
    rig_init(&r, true, 150, 400);
    r.reliable = true;
    learn(&r, HM(6, 0), HM(20, 0));
    r.min0 = HM(21, 0);
    r.p.frost_on = false;
    seed(&r, 150, 400);
    for (int m = 1; m <= 62; m++) {
        r.col = (int16_t)(150 + m / 3);
        r.tank = (int16_t)(400 - m / 5);
        run(&r, 60);
    }
    CHECK_EQ(r.starts, 0);
    CHECK(r.e8.sign);
    CHECK_EQ(r.show_col, 150);
    /* a pulse restarts the hour (the pump ran) */
    CHECK(until_on(&r, 3 * 3600));
    step(&r);
    CHECK_EQ(r.e8.n, 0);
    /* the normal cooling is learned from the tank pipe: the night ends, a normal night is counted */
    rig_init(&r, true, 150, 400);
    r.reliable = true;
    learn(&r, HM(6, 0), HM(20, 0));
    r.min0 = HM(21, 0);
    r.p.frost_on = false;
    seed(&r, 150, 400);
    for (int m = 1; m <= 150; m++) {
        r.tank = (int16_t)(400 - m / 10);               /* 0.6 K/h */
        run(&r, 60);
    }
    while (rig_min(&r) != HM(6, 1)) {
        step(&r);
    }
    CHECK_EQ(r.e8.l.normal_nights, 1);
    CHECK(sh_e8_normal(&r.e8.l) != SH_E8_NO_RATE);
}

/* E6 only in a continuous run of more than 10 min, never by a pulse. */
static void test_e6(void)
{
    rig_t r;
    rig_init(&r, true, 900, 895);
    r.reliable = true;
    r.min0 = HM(10, 0);
    /* pulses every 3 min with a collector that climbs fast and a tank that does not: no E6 */
    for (int i = 0; i < 20; i++) {
        CHECK(until_on(&r, 3600));
        r.col = (int16_t)(r.col + 10);
        r.tank = (int16_t)(r.col - 5);
        pulse_out(&r);
    }
    CHECK(!(r.alarms & SH_ALARM_BIT(SH_ALARM_E6)));
    /* the same in a long run: the collector +10 K in 10 min, the tank still, 20 K apart -> E6 */
    rig_init(&r, true, 700, 400);
    r.reliable = true;
    r.min0 = HM(10, 0);
    CHECK(until_on(&r, 3600));
    for (int m = 0; m < 25; m++) {
        r.col = (int16_t)(r.col + 12);
        run(&r, 60);
    }
    CHECK(r.pump.on);
    CHECK(r.alarms & SH_ALARM_BIT(SH_ALARM_E6));
}

/* The pump test, the air venting and the anti-seize run are unchanged and give reliable values. */
static void test_test_air_seize(void)
{
    rig_t r;
    rig_init(&r, true, 300, 400);
    r.reliable = true;
    r.min0 = HM(20, 0);
    run(&r, 10);
    sh_pump_ctl_plus(&r.pump, 300, r.now);
    step(&r);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_TEST);
    run(&r, 28);
    CHECK_EQ(r.show_col, NONE);
    run(&r, 2);
    CHECK_EQ(r.show_col, 300);                          /* after 30 s of any run */
    run(&r, 300);
    CHECK(!r.pump.on);                                  /* the test is over: no difference, off */
    const uint32_t meas = r.pulse.meas_s;
    CHECK(until_on(&r, 4 * 3600));
    CHECK_EQ(r.now - 1, meas + 3 * 3600);               /* the timers count from the values of the test */
    pulse_out(&r);
    /* a test that ends with a difference: the pump goes on charging without a stop */
    r.col = 500;
    sh_pump_ctl_plus(&r.pump, 60, r.now);
    const uint32_t starts = r.starts;
    run(&r, 70);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_DIFF);
    CHECK_EQ(r.starts, starts + 1);
    /* air venting: run / pause as always */
    r.col = 300;
    run(&r, 5);
    CHECK(!r.pump.on);
    sh_pump_ctl_air_start(&r.pump, 600, r.now);
    step(&r);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_AIR_RUN);
    run(&r, 300);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_AIR_PAUSE);
    sh_pump_ctl_air_stop(&r.pump);
    run(&r, 2);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_PULSE_WAIT);
    /* anti-seize: after 24 h without running (no pulse was due meanwhile) */
    sh_pump_ctl_t c;
    memset(&c, 0, sizeof(c));
    sh_pump_params_t p = P;
    p.pulse = true;
    p.min_run_s = SH_PULSE_S;
    const sh_pump_temps_t t = { .t_col = 500, .t_tank = 400, .pipe_tank = 400 };
    sh_pump_ctl_step(&c, &p, &t, SH_SEIZE_IDLE_S - 1);
    CHECK(!c.on && c.reason == SH_PUMP_REASON_PULSE_WAIT);
    sh_pump_ctl_step(&c, &p, &t, SH_SEIZE_IDLE_S);
    CHECK(c.on && c.reason == SH_PUMP_REASON_ANTI_SEIZE);
}

/* The sensor-fault modes: the tank by the collector with pulses, the collector by its probes. */
static void test_faults(void)
{
    rig_t r;
    /* tank sensor faulty: a pulse, then by the collector >= 60 °C */
    rig_init(&r, true, 599, 400);
    r.reliable = true;
    r.min0 = HM(10, 0);
    r.tank_fault = true;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_PULSE);
    CHECK(until_off(&r, 40));
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_TF_COLD);
    step(&r);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_PULSE_WAIT);
    uint32_t meas = r.pulse.meas_s;
    r.col = 600;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, meas + 600);                    /* 59.9 °C: every 10 min */
    run(&r, 31);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_TF_RUN);
    r.col = 950;                                        /* 95 °C while running: off at once */
    step(&r);
    CHECK(!r.pump.on);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_TF_HOT);
    meas = r.pulse.meas_s;
    r.col = 700;
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, meas + 180);                    /* above 80 °C: every 3 min */
    run(&r, 31);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_TF_RUN);
    r.col = 579;
    run(&r, 2);
    CHECK(!r.pump.on);

    /* collector sensor faulty: no pulses, the probes of 10 min as always */
    rig_init(&r, true, 300, 400);
    r.reliable = true;
    r.min0 = HM(10, 0);
    r.col_fault = true;
    run(&r, 119);
    CHECK(!r.pump.on);
    run(&r, 2);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_CF_PROBE);
    run(&r, 598);
    CHECK(r.pump.on);
    CHECK_EQ(r.show_tank, 400);                         /* the tank of the flow is taken */
    run(&r, 3);
    CHECK(!r.pump.on && r.pump.reason == SH_PUMP_REASON_CF_WAIT);
    /* both faulty: off */
    r.tank_fault = true;
    run(&r, 3);
    CHECK(!r.pump.on && r.pump.reason == SH_PUMP_REASON_BOTH_FAULT);
}

/* ---- the mode itself (3.1, 3.5) -------------------------------------------------------------- */

static void test_mode_switch(void)
{
    rig_t r;
    rig_init(&r, false, 300, 400);
    r.reliable = true;
    r.min0 = HM(9, 0);
    run(&r, 3600);
    CHECK_EQ(r.starts, 0);                              /* in sleeves there are no pulses */
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_DIFF);
    CHECK_EQ(r.show_col, 300);
    /* switched to the pipes: at once; nothing reliable yet, the timers start now */
    r.pipes = true;
    step(&r);
    CHECK(r.pulse.events & SH_PULSE_EVT_MODE);
    CHECK_EQ(r.show_col, NONE);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_PULSE_WAIT);
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, 3600 + 1800);
    pulse_out(&r);
    CHECK_EQ(r.show_col, 300);
    /* back to the sleeves and to the pipes again: the reliable values are gone, the timers start over */
    r.pipes = false;
    step(&r);
    CHECK_EQ(r.show_col, 300);
    CHECK_EQ(r.pump.reason, SH_PUMP_REASON_DIFF);
    r.pipes = true;
    step(&r);
    const uint32_t at = r.now - 1;
    CHECK(!r.pulse.have);
    CHECK_EQ(r.show_col, NONE);
    CHECK(until_on(&r, 3600));
    CHECK_EQ(r.now - 1, at + 1800);
    /* switched while the pump charges (sleeves, a run of minutes): the values of the flow are
     * reliable at once and the pump goes on */
    rig_init(&r, false, 500, 400);
    r.reliable = true;
    r.min0 = HM(9, 0);
    run(&r, 300);
    CHECK(r.pump.on);
    r.pipes = true;
    run(&r, 2);
    CHECK(r.pump.on && r.pump.reason == SH_PUMP_REASON_DIFF);
    CHECK_EQ(r.show_col, 500);
}

/* With sleeves everything is as it was: the same run with and without the new inputs. */
static void test_sleeves_unchanged(void)
{
    sh_pump_ctl_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    sh_pump_params_t pa = P, pb = P;
    pb.pulse_due = true;                                /* ignored without `pulse` */
    pb.pulse_fresh = true;
    int16_t col = 200, tank = 400;
    for (uint32_t s = 0; s < 6 * 3600; s++) {
        col = (int16_t)(200 + (s / 30) % 500);          /* a collector that warms and cools */
        const sh_pump_temps_t ta = { .t_col = col, .t_tank = tank };
        const sh_pump_temps_t tb = { .t_col = col, .t_tank = tank, .pipe_tank = 999 };
        sh_pump_ctl_step(&a, &pa, &ta, s);
        sh_pump_ctl_step(&b, &pb, &tb, s);
        if (a.on != b.on || a.reason != b.reason) {
            CHECK(false);
            break;
        }
        CHECK(a.reason != SH_PUMP_REASON_PULSE && a.reason != SH_PUMP_REASON_PULSE_WAIT);
    }
    CHECK(a.switched_s == b.switched_s && a.switched_s != 0);
    /* sh_pulse does nothing without the pipes */
    sh_pulse_t p;
    memset(&p, 0, sizeof(p));
    sh_pulse_in_t in = { .pipes = false, .t_col = 50, .t_tank = 300, .pump_on = true, .run_s = 100,
                         .boiling = 1277, .frost_on = true, .frost = 40, .cooling = true };
    sh_pulse_measure(&p, &in, 100);
    CHECK(!p.fresh && !p.have && p.events == 0);
    in.pump_on = false;
    CHECK(!sh_pulse_plan(&p, &in, 100000));
    CHECK(!p.next_known && !p.frost_watch);
    /* the names of the new reasons */
    CHECK_EQ(strcmp(sh_pump_reason_name(SH_PUMP_REASON_PULSE), "pulse"), 0);
    CHECK_EQ(strcmp(sh_pump_reason_name(SH_PUMP_REASON_PULSE_WAIT), "waiting for the next pulse"), 0);
}

int main(void)
{
    test_installer();
    test_window();
    test_intervals();
    test_pulse_cycle();
    test_day_intervals();
    test_col_protect();
    test_overheat();
    test_night_pulse();
    test_cooling();
    test_vacation();
    test_frost();
    test_sun();
    test_no_leak();
    test_heater_e8();
    test_e6();
    test_test_air_seize();
    test_faults();
    test_mode_switch();
    test_sleeves_unchanged();
    return test_summary("test_group_d2");
}
