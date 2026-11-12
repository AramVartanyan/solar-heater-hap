/* Host test of the clock and the history (doc/tasks/step9-clock-history.md 3.1 - 3.3):
 * the time of the browser, the trust of the clock, the zone check by the collector peak
 * (sh_clock_rules); the time zones (sh_tz); the chart and the summary of the day (sh_day); the
 * daily record, the ring in the flash and the CSV (sh_hist). */

#include <stdio.h>
#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_clock_rules.h"
#include "sh_tz.h"
#include "sh_day.h"
#include "sh_hist.h"

/* ---- clock rules ------------------------------------------------------------------------ */

#define NOV_2026    1794470400LL        /* 2026-11-12 00:00 UTC */

static void test_browser(void)
{
    /* never set by SNTP (no internet, AP mode, after a power cut) -> taken */
    CHECK(sh_clock_browser_ok(NOV_2026, false, 0, 100));
    /* SNTP in the last 24 h -> not taken; 24 h after it -> taken */
    CHECK(!sh_clock_browser_ok(NOV_2026, true, 1000, 1000));
    CHECK(!sh_clock_browser_ok(NOV_2026, true, 1000, 1000 + 24 * 3600 - 1));
    CHECK(sh_clock_browser_ok(NOV_2026, true, 1000, 1000 + 24 * 3600));
    /* a time out of 2020 ... 2099 is never taken */
    CHECK(!sh_clock_browser_ok(0, false, 0, 100));
    CHECK(!sh_clock_browser_ok(SH_CLOCK_EPOCH_MIN - 1, false, 0, 100));
    CHECK(sh_clock_browser_ok(SH_CLOCK_EPOCH_MIN, false, 0, 100));
    CHECK(!sh_clock_browser_ok(SH_CLOCK_EPOCH_MAX, false, 0, 100));
}

static void test_trust(void)
{
    CHECK_EQ(sh_clock_trust(false, true, true), SH_CLOCK_UNSET);
    /* a zone not confirmed: never reliable, also with good peaks */
    CHECK_EQ(sh_clock_trust(true, false, true), SH_CLOCK_TZ_UNCONFIRMED);
    CHECK_EQ(sh_clock_trust(true, false, false), SH_CLOCK_TZ_UNCONFIRMED);
    CHECK_EQ(sh_clock_trust(true, true, false), SH_CLOCK_UNRELIABLE);
    CHECK_EQ(sh_clock_trust(true, true, true), SH_CLOCK_RELIABLE);
}

/* A full sunny day (collector 15 -> 65 °C) with the peak at `min`. */
static sh_peak_day_t sunny(sh_peak_t *p, int min)
{
    return sh_peak_day(p, true, true, 150, 650, (int16_t)min);
}

static sh_peak_day_t cloudy(sh_peak_t *p)
{
    return sh_peak_day(p, true, true, 150, 340, 12 * 60);      /* 19 K */
}

static void test_peak(void)
{
    sh_peak_t p;
    /* the window 10:00 - 17:00, both ends in, +-1 min out */
    sh_peak_reset(&p, true);
    CHECK_EQ(sunny(&p, 9 * 60 + 59), SH_PEAK_OUT);
    CHECK_EQ(sunny(&p, 10 * 60), SH_PEAK_IN);
    CHECK_EQ(sunny(&p, 17 * 60), SH_PEAK_IN);
    CHECK_EQ(sunny(&p, 17 * 60 + 1), SH_PEAK_OUT);
    CHECK_EQ(p.peak, 17 * 60 + 1);

    /* a confirmed zone is reliable at once; one day out does not change it, two in a row do */
    sh_peak_reset(&p, true);
    CHECK_EQ(p.peak, SH_PEAK_NONE);
    sunny(&p, 21 * 60);
    CHECK(p.reliable);
    sunny(&p, 22 * 60);
    CHECK(!p.reliable);
    /* one day in: still not reliable; the second in a row: reliable again */
    sunny(&p, 14 * 60);
    CHECK(!p.reliable);
    sunny(&p, 15 * 60);
    CHECK(p.reliable);

    /* the series: out, in, out -> no two in a row, still reliable */
    sh_peak_reset(&p, true);
    sunny(&p, 3 * 60);
    sunny(&p, 13 * 60);
    sunny(&p, 3 * 60);
    CHECK(p.reliable);
    CHECK_EQ(p.out_row, 1);

    /* cloudy days are not counted and do not break the series */
    sh_peak_reset(&p, true);
    sunny(&p, 3 * 60);
    CHECK_EQ(cloudy(&p), SH_PEAK_NOT_COUNTED);
    CHECK_EQ(cloudy(&p), SH_PEAK_NOT_COUNTED);
    CHECK(p.reliable);
    CHECK_EQ(p.peak, 3 * 60);                   /* the last counted day */
    sunny(&p, 4 * 60);
    CHECK(!p.reliable);
    sunny(&p, 12 * 60);
    CHECK_EQ(cloudy(&p), SH_PEAK_NOT_COUNTED);
    sunny(&p, 12 * 60);
    CHECK(p.reliable);

    /* sunny: >= 20 K above the minimum of the day */
    sh_peak_reset(&p, true);
    CHECK_EQ(sh_peak_day(&p, true, true, 150, 349, 3 * 60), SH_PEAK_NOT_COUNTED);
    CHECK_EQ(sh_peak_day(&p, true, true, 150, 350, 3 * 60), SH_PEAK_OUT);
    CHECK_EQ(sh_peak_day(&p, true, true, -100, 100, 3 * 60), SH_PEAK_OUT);
    CHECK(!p.reliable);

    /* partial days and days with a faulty collector sensor are not counted */
    sh_peak_reset(&p, true);
    CHECK_EQ(sh_peak_day(&p, false, true, 150, 650, 3 * 60), SH_PEAK_NOT_COUNTED);
    CHECK_EQ(sh_peak_day(&p, true, false, 150, 650, 3 * 60), SH_PEAK_NOT_COUNTED);
    CHECK_EQ(sh_peak_day(&p, true, true, 150, 650, -1), SH_PEAK_NOT_COUNTED);
    CHECK_EQ(p.out_row, 0);
    CHECK(p.reliable);

    /* the reset of the learned values with a zone not confirmed: not reliable by the peaks */
    sh_peak_reset(&p, false);
    CHECK(!p.reliable);
    CHECK_EQ(p.in_row, 0);
}

static void test_dates(void)
{
    CHECK_EQ(sh_day_number(2020, 1, 1), 0);
    CHECK_EQ(sh_day_number(2020, 2, 29), 59);
    CHECK_EQ(sh_day_number(2020, 3, 1), 60);                /* 2020 is a leap year */
    CHECK_EQ(sh_day_number(2021, 1, 1), 366);
    CHECK_EQ(sh_day_number(2026, 11, 12), (NOV_2026 - SH_CLOCK_EPOCH_MIN) / 86400);
    CHECK_EQ(sh_day_number(2019, 12, 31), SH_DAY_NONE);
    CHECK_EQ(sh_day_number(2100, 1, 1), SH_DAY_NONE);
    /* back and forth over the whole range */
    int bad = 0;
    for (unsigned d = 0; d <= sh_day_number(2099, 12, 31); d++) {
        int y, m, md;
        sh_day_date((uint16_t)d, &y, &m, &md);
        bad += sh_day_number(y, m, md) != d;
    }
    CHECK_EQ(bad, 0);
    int y, m, md;
    sh_day_date(sh_day_number(2028, 2, 29), &y, &m, &md);
    CHECK(y == 2028 && m == 2 && md == 29);
}

/* ---- time zones ------------------------------------------------------------------------- */

static void test_tz(void)
{
    CHECK(strcmp(sh_tz_list[SH_TZ_DEFAULT].name, "UTC") == 0);
    CHECK_EQ(sh_tz_find("UTC"), SH_TZ_DEFAULT);
    const int sofia = sh_tz_find("Europe/Sofia");
    CHECK(sofia > 0);
    CHECK(sofia >= 0 && strcmp(sh_tz_list[sofia].rule, "EET-2EEST,M3.5.0/3,M10.5.0/4") == 0);
    CHECK_EQ(sh_tz_find("Mars/Olympus"), -1);
    CHECK_EQ(sh_tz_find(NULL), -1);
    for (size_t i = 0; i < sh_tz_count; i++) {
        CHECK(strlen(sh_tz_list[i].name) < SH_TZ_NAME_MAX);
        CHECK(strlen(sh_tz_list[i].rule) > 0 && strlen(sh_tz_list[i].rule) < SH_TZ_RULE_MAX);
        CHECK_EQ(sh_tz_find(sh_tz_list[i].name), (long)i);     /* no name twice */
    }
}

/* ---- the day in RAM --------------------------------------------------------------------- */

static void test_kinds(void)
{
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_DIFF), -1);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_DIFF), SH_RUN_CHARGE);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_MIN_RUN), SH_RUN_CHARGE);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_TF_RUN), SH_RUN_CHARGE);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_CF_RUN), SH_RUN_CHARGE);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_COL_PROTECT), SH_RUN_PROTECT);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_FROST), SH_RUN_PROTECT);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_TEST), SH_RUN_OTHER);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_ANTI_SEIZE), SH_RUN_OTHER);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_TF_PROBE), SH_RUN_PROTECT);   /* Hot, orange */
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_CF_PROBE), SH_RUN_OTHER);
    /* group C */
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_COOL), SH_RUN_COOL);
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_COOL_WAIT), -1);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_AIR_RUN), SH_RUN_OTHER);
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_AIR_PAUSE), -1);
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_VACATION), -1);
}


static sh_day_t s_d;
static sh_day_sum_t s_done;

typedef struct {
    sh_chart_slot_t s[SH_CHART_SLOTS];
    unsigned n;
} slots_t;

static void take_slot(void *ctx, const sh_chart_slot_t *s)
{
    slots_t *c = ctx;
    c->s[c->n++] = *s;
}

static unsigned chart(slots_t *c, const sh_local_t *lt, uint32_t now)
{
    c->n = 0;
    return sh_day_chart(&s_d, lt, now, take_slot, c);
}

/* Steps of 1 s from `from` to `to` (excluded) with the local time running from `day`, `min0` (if
 * `set`); returns how many days ended. */
static int run(uint32_t from, uint32_t to, bool set, uint16_t day, uint32_t sec0, const sh_day_in_t *in)
{
    int ended = 0;
    for (uint32_t t = from; t < to; t++) {
        const uint32_t ls = sec0 + (t - from);
        const sh_local_t lt = { .set = set, .day = (uint16_t)(day + ls / 86400), .min = (uint16_t)(ls % 86400 / 60) };
        ended += sh_day_step(&s_d, in, &lt, t, &s_done);
    }
    return ended;
}

static void test_chart_clock(void)
{
    sh_day_init(&s_d);
    const sh_day_in_t charge = { .t_col = 500, .t_tank = 300, .pump_on = true, .reason = SH_PUMP_REASON_DIFF };
    const sh_day_in_t idle = { .t_col = 250, .t_tank = 310, .pump_on = false, .reason = SH_PUMP_REASON_DIFF };
    const sh_day_in_t test = { .t_col = 260, .t_tank = 310, .pump_on = true, .reason = SH_PUMP_REASON_TEST };
    const uint16_t D = 2507;

    /* with a set clock the intervals follow the local time: 00:00 - 00:10, ... */
    run(100, 100 + 7 * 60, true, D, 0, &charge);             /* 00:00 - 00:07 charging */
    run(100 + 7 * 60, 100 + 20 * 60, true, D, 7 * 60, &idle);
    run(100 + 20 * 60, 100 + 21 * 60 + 30, true, D, 20 * 60, &test);
    const sh_local_t lt = { .set = true, .day = D, .min = 21 };
    slots_t c;
    CHECK_EQ(chart(&c, &lt, 100 + 21 * 60 + 30), 2);
    CHECK_EQ(c.s[0].idx, 0);
    CHECK_EQ(c.s[0].run_s[SH_RUN_CHARGE], 7 * 60);
    CHECK_EQ(c.s[0].t_col, 250);                            /* the temperatures at the end */
    CHECK_EQ(c.s[1].idx, 1);
    CHECK_EQ(c.s[1].run_s[SH_RUN_CHARGE] + c.s[1].run_s[SH_RUN_OTHER], 0);
    /* run time today: the closed intervals and the running one */
    uint32_t r[SH_RUN_KINDS];
    sh_day_run_today(&s_d, &lt, 100 + 21 * 60 + 30, r);
    CHECK_EQ(r[SH_RUN_CHARGE], 7 * 60);
    CHECK_EQ(r[SH_RUN_PROTECT], 0);
    CHECK_EQ(r[SH_RUN_OTHER], 89);                          /* 90 s, the last second is in the next step */

    /* a faulty sensor: no temperature at the end of the interval */
    sh_day_in_t faulty = idle;
    faulty.col_fault = true;
    run(100 + 21 * 60 + 30, 100 + 30 * 60 + 1, true, D, 21 * 60 + 30, &faulty);
    chart(&c, &lt, 100 + 30 * 60 + 1);
    CHECK_EQ(c.n, 3);
    CHECK_EQ(c.s[2].t_col, SH_TEMP_INVALID);
    CHECK_EQ(c.s[2].t_tank, 310);
}

static void test_chart_no_clock(void)
{
    /* without a clock: intervals of 10 min from the start, the chart is the last 24 h */
    sh_day_init(&s_d);
    const sh_day_in_t prot = { .t_col = 1150, .t_tank = 600, .pump_on = true, .reason = SH_PUMP_REASON_COL_PROTECT };
    run(0, 25 * 3600 + 1, false, 0, 0, &prot);
    const sh_local_t none = { .set = false };
    slots_t c;
    CHECK_EQ(chart(&c, &none, 25 * 3600 + 1), SH_CHART_SLOTS - 1);  /* + the running one = 24 h */
    CHECK_EQ(c.s[0].end_s, 3600 + 2 * 600);                         /* the oldest: ended < 23 h 50 min ago */
    CHECK_EQ(c.s[c.n - 1].end_s, 25 * 3600);
    CHECK_EQ(c.s[0].day, SH_DAY_NONE);
    uint32_t r[SH_RUN_KINDS];
    sh_day_run_today(&s_d, &none, 25 * 3600 + 1, r);
    CHECK_EQ(r[SH_RUN_PROTECT], 24 * 3600 - 600 + 0);               /* 143 intervals + the running 0 s */
    /* the ring keeps 144 intervals */
    CHECK_EQ(s_d.count, SH_CHART_SLOTS);

    /* the clock is set: the chart is from midnight, the intervals before are not on it */
    const uint16_t D = 2600;
    run(25 * 3600 + 1, 25 * 3600 + 1 + 1200, true, D, 13 * 3600 + 5 * 60, &prot);  /* 13:05 - 13:25 */
    const sh_local_t lt = { .set = true, .day = D, .min = 13 * 60 + 25 };
    CHECK_EQ(chart(&c, &lt, 25 * 3600 + 1 + 1200), 2);              /* 13:05 - 13:10, 13:10 - 13:20 */
    CHECK_EQ(c.s[0].idx, 78);
    CHECK_EQ(c.s[1].idx, 79);
    CHECK_EQ(c.s[0].run_s[SH_RUN_PROTECT], 300);
    sh_day_run_today(&s_d, &lt, 25 * 3600 + 1 + 1200, r);
    CHECK_EQ(r[SH_RUN_PROTECT], 1199);                              /* since the clock was set */
}

static void test_summary_day(void)
{
    sh_day_init(&s_d);
    const uint16_t D = 2700;
    sh_day_in_t in = { .t_col = 150, .t_tank = 400, .pump_on = false, .reason = SH_PUMP_REASON_DIFF };

    /* the clock is set at 11:00: the day is partial */
    CHECK_EQ(run(0, 3600, true, D, 11 * 3600, &in), 0);
    in.t_col = 700;                                 /* 12:00: the maximum, twice -> the first one */
    in.pump_on = true;
    CHECK_EQ(run(3600, 3660, true, D, 12 * 3600, &in), 0);
    in.t_col = 690;
    run(3660, 7200, true, D, 12 * 3600 + 60, &in);
    in.t_col = 700;
    run(7200, 7260, true, D, 13 * 3600, &in);
    in.pump_on = false;
    in.t_col = 120;
    in.t_tank = 550;
    in.alarms = SH_ALARM_BIT(SH_ALARM_E6);
    run(7260, 7320, true, D, 13 * 3600 + 60, &in);
    in.alarms = 0;
    in.tank_fault = true;                           /* the faulty tank: not in the minimum / maximum */
    in.t_tank = 900;
    in.col_fault = true;
    in.t_col = 1500;
    run(7320, 7380, true, D, 13 * 3600 + 120, &in);
    in.tank_fault = in.col_fault = false;
    in.t_tank = 550;
    in.t_col = 120;
    /* to midnight and over it */
    CHECK_EQ(run(7380, 7380 + 10 * 3600 + 57 * 60, true, D, 13 * 3600 + 180, &in), 0);
    CHECK_EQ(run(7380 + 10 * 3600 + 57 * 60, 7380 + 10 * 3600 + 58 * 60, true, D, 23 * 3600 + 59 * 60, &in), 0);
    CHECK_EQ(run(7380 + 10 * 3600 + 58 * 60, 7380 + 10 * 3600 + 58 * 60 + 2, true, D, 23 * 3600 + 59 * 60 + 60, &in), 1);
    CHECK_EQ(s_done.day, D);
    CHECK(s_done.valid && !s_done.full);
    CHECK_EQ(s_done.col_max, 700);
    CHECK_EQ(s_done.peak_min, 12 * 60);
    CHECK_EQ(s_done.col_min, 120);
    CHECK_EQ(s_done.tank_max, 550);
    CHECK_EQ(s_done.tank_min, 400);
    CHECK(s_done.col_fault);
    CHECK_EQ(s_done.run_s[SH_RUN_CHARGE], 3660);
    CHECK_EQ(s_done.alarms, SH_ALARM_BIT(SH_ALARM_E6));

    /* the next day from midnight without a break: full */
    const uint32_t t0 = 7380 + 10 * 3600 + 58 * 60 + 2;
    CHECK(s_d.sum.full);
    CHECK_EQ(run(t0, t0 + 86400 - 2, true, (uint16_t)(D + 1), 2, &in), 0);       /* 00:00:02 - 23:59:59 */
    CHECK_EQ(run(t0 + 86400 - 2, t0 + 86400, true, (uint16_t)(D + 1), 86400, &in), 1);
    CHECK_EQ(s_done.day, D + 1);
    CHECK(s_done.full);
    CHECK(!s_done.col_fault);

    /* the clock set to another date (not a midnight): the collected day is dropped, the next partial */
    const uint32_t t1 = t0 + 86400;
    CHECK_EQ(run(t1, t1 + 60, true, (uint16_t)(D + 5), 10 * 3600, &in), 0);
    CHECK(!s_d.sum.full);
    CHECK_EQ(s_d.sum.day, D + 5);
    /* a date that goes back is no midnight either */
    CHECK_EQ(run(t1 + 60, t1 + 120, true, (uint16_t)(D + 4), 23 * 3600, &in), 0);
    /* the next date after 01:00 (e.g. the clock was late): no record */
    CHECK_EQ(run(t1 + 120, t1 + 180, true, (uint16_t)(D + 5), 3600, &in), 0);

    /* no clock: nothing is collected */
    sh_day_init(&s_d);
    CHECK_EQ(run(0, 2 * 86400, false, 0, 0, &in), 0);
    CHECK(!s_d.sum.valid);
}

/* Group C: the minutes of cooling, the sunrise / sunset and the cooling target of the day */
static void test_summary_group_c(void)
{
    sh_day_init(&s_d);
    const uint16_t D = 2800;
    sh_day_in_t in = { .t_col = 400, .t_tank = 700, .pump_on = true, .reason = SH_PUMP_REASON_COOL,
                       .sunrise = -1, .sunset = -1, .cool_target = 663 };
    sh_local_t lt = { .set = true, .day = D, .min = 0 };
    sh_day_sum_t done;
    uint32_t t = 0;
    for (; t < 3 * 3600; t += 60) {                 /* 00:00 - 03:00: cooling */
        lt.min = (uint16_t)(t / 60);
        sh_day_step(&s_d, &in, &lt, t, &done);
    }
    in.pump_on = false;
    in.reason = SH_PUMP_REASON_COOL_WAIT;
    in.sunrise = 6 * 60 + 41;
    for (; t < 18 * 3600; t += 60) {
        lt.min = (uint16_t)(t / 60);
        sh_day_step(&s_d, &in, &lt, t, &done);
    }
    in.sunset = 17 * 60 + 2;
    in.cool_target = 633;
    for (; t < 24 * 3600; t += 60) {
        lt.min = (uint16_t)(t / 60);
        sh_day_step(&s_d, &in, &lt, t, &done);
    }
    lt.day = D + 1;
    lt.min = 0;
    in.sunrise = in.sunset = -1;                    /* the new day: none yet */
    CHECK(sh_day_step(&s_d, &in, &lt, t, &done));
    CHECK_EQ(done.run_s[SH_RUN_COOL], 3 * 3600);
    CHECK_EQ(done.run_s[SH_RUN_CHARGE], 0);
    CHECK_EQ(done.sunrise, 6 * 60 + 41);
    CHECK_EQ(done.sunset, 17 * 60 + 2);
    CHECK_EQ(done.cool_target, 633);
    CHECK_EQ(s_d.sum.sunrise, -1);
}

/* The local time jumps within the day (from the other experiment variant, step 9, PR #35). */
static void test_clock_jumps(void)
{
    const uint16_t D = 2800;
    sh_day_in_t in = { .t_col = 300, .t_tank = 400, .pump_on = false, .reason = SH_PUMP_REASON_DIFF };

    /* from midnight; daylight saving time +1 h at 03:00, back -1 h, an SNTP correction of 30 s:
     * the day goes on, full */
    sh_day_init(&s_d);
    run(0, 60, true, (uint16_t)(D - 1), 86400 - 60, &in);   /* the day before, its last minute */
    uint32_t t = 60;
    run(t, t + 3 * 3600, true, D, 0, &in);                  /* 00:00 - 03:00 */
    t += 3 * 3600;
    CHECK(s_d.sum.full);
    run(t, t + 3600, true, D, 4 * 3600, &in);               /* spring: 03:00 -> 04:00 */
    t += 3600;
    CHECK(s_d.sum.valid && s_d.sum.full);
    run(t, t + 3600, true, D, 4 * 3600, &in);               /* autumn: 05:00 -> 04:00 */
    t += 3600;
    CHECK(s_d.sum.full);
    run(t, t + 600, true, D, 5 * 3600 + 30, &in);           /* SNTP: +30 s */
    t += 600;
    CHECK(s_d.sum.full);
    in.t_col = 900;                                         /* a maximum at the right time */
    run(t, t + 60, true, D, 5 * 3600 + 630, &in);
    t += 60;
    CHECK_EQ(s_d.sum.col_max, 900);

    /* the clock set anew by 3 h within the day: the collected part is dropped, the day is partial */
    in.t_col = 500;
    run(t, t + 60, true, D, 9 * 3600, &in);
    t += 60;
    CHECK(s_d.sum.valid && !s_d.sum.full);
    CHECK_EQ(s_d.sum.day, D);
    CHECK_EQ(s_d.sum.col_max, 500);                         /* the 90.0 °C before the jump is gone */

    /* exactly 61 min still goes on, 62 min does not */
    sh_day_init(&s_d);
    run(0, 60, true, (uint16_t)(D - 1), 86400 - 60, &in);
    t = 60;
    run(t, t + 600, true, D, 0, &in);
    t += 600;
    run(t, t + 60, true, D, 9 * 60 + 61 * 60, &in);        /* last step 00:09:59 -> 01:10 */
    t += 60;
    CHECK(s_d.sum.full);
    run(t, t + 60, true, D, 70 * 60 + 62 * 60, &in);        /* last step 01:10:59 -> 02:12 */
    CHECK(!s_d.sum.full);
}

/* ---- daily record, ring, CSV ------------------------------------------------------------ */

#define FLASH_SIZE      (64 * 1024)
#define SECTOR          4096

static uint8_t s_flash[FLASH_SIZE];
static unsigned s_erases[FLASH_SIZE / SECTOR];
static bool s_fail_write;

static bool f_read(void *ctx, uint32_t off, void *buf, uint32_t len)
{
    (void)ctx;
    if (off + len > FLASH_SIZE) {
        return false;
    }
    memcpy(buf, s_flash + off, len);
    return true;
}

static bool f_write(void *ctx, uint32_t off, const void *buf, uint32_t len)
{
    (void)ctx;
    if (off + len > FLASH_SIZE || s_fail_write) {
        return false;
    }
    for (uint32_t i = 0; i < len; i++) {
        s_flash[off + i] &= ((const uint8_t *)buf)[i];     /* a write only clears bits */
    }
    return true;
}

static bool f_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    if (off % SECTOR || len % SECTOR || off + len > FLASH_SIZE) {
        return false;
    }
    memset(s_flash + off, 0xFF, len);
    for (uint32_t s = off / SECTOR; s < (off + len) / SECTOR; s++) {
        s_erases[s]++;
    }
    return true;
}

static const sh_hist_flash_t FLASH = {
    .read = f_read, .write = f_write, .erase = f_erase, .size = FLASH_SIZE, .sector = SECTOR,
};

static sh_hist_rec_t rec(uint16_t day)
{
    sh_hist_rec_t r;
    sh_hist_rec_init(&r, day);
    r.col_max = (int16_t)(day % 1000);
    r.run_min[0] = day;
    return r;
}

typedef struct {
    uint16_t day[2100];
    unsigned n;
} days_t;

static bool take_day(void *ctx, const sh_hist_rec_t *r)
{
    days_t *d = ctx;
    d->day[d->n++] = r->day;
    return true;
}

static days_t s_days;

static unsigned walk(const sh_hist_t *h, bool newest_first)
{
    s_days.n = 0;
    return sh_hist_walk(h, newest_first, 0, take_day, &s_days);
}

static void test_record(void)
{
    sh_hist_rec_t r;
    sh_hist_rec_init(&r, sh_day_number(2026, 11, 11));
    r.col_max = 1153;
    r.col_min = -52;
    r.tank_max = 611;
    r.tank_min = SH_HIST_NO_TEMP;
    r.run_min[0] = 190;
    r.run_min[1] = 4;
    r.run_min[2] = 1;
    r.alarms = SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_HOT);
    r.peak_min = 13 * 60 + 10;
    r.clock_reliable = true;
    r.partial = true;
    uint8_t b[SH_HIST_REC_SIZE];
    sh_hist_encode(&r, b);
    CHECK_EQ(b[0] | b[1] << 8, sh_day_number(2026, 11, 11));
    CHECK_EQ(b[22] | b[23] << 8, 0x8000 | (13 * 60 + 10));
    CHECK_EQ(b[24], 0xFF);                                  /* learned: no data */
    CHECK_EQ(b[28], 0xFF);
    CHECK_EQ(b[29], SH_HIST_VERSION);
    CHECK_EQ(b[31], 0x01);                                  /* partial */
    sh_hist_rec_t back;
    CHECK_EQ(sh_hist_decode(b, &back), SH_HIST_VALID);
    CHECK_EQ(back.day, r.day);
    CHECK_EQ(back.col_max, 1153);
    CHECK_EQ(back.col_min, -52);
    CHECK_EQ(back.tank_min, SH_HIST_NO_TEMP);
    CHECK_EQ(back.run_min[2], 1);
    CHECK_EQ(back.alarms, r.alarms);
    CHECK_EQ(back.peak_min, 13 * 60 + 10);
    CHECK(back.clock_reliable && back.partial);
    CHECK_EQ(back.sunrise, SH_HIST_NO_TIME);
    CHECK_EQ(back.cool_target, SH_HIST_NO_U8);

    /* a damaged record: any bit of it, the version, the checksum */
    int bad = 0;
    for (int i = 0; i < SH_HIST_REC_SIZE; i++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t c[SH_HIST_REC_SIZE];
            memcpy(c, b, sizeof(c));
            c[i] ^= (uint8_t)(1u << bit);
            bad += sh_hist_decode(c, &back) != SH_HIST_DAMAGED;
        }
    }
    CHECK_EQ(bad, 0);
    uint8_t e[SH_HIST_REC_SIZE];
    memset(e, 0xFF, sizeof(e));
    CHECK_EQ(sh_hist_decode(e, &back), SH_HIST_EMPTY);
    memset(e, 0, sizeof(e));
    CHECK_EQ(sh_hist_decode(e, &back), SH_HIST_DAMAGED);

    /* no peak */
    r.peak_min = SH_HIST_NO_PEAK;
    r.clock_reliable = false;
    sh_hist_encode(&r, b);
    CHECK_EQ(b[22] | b[23] << 8, 0x07FF);
    CHECK_EQ(sh_hist_decode(b, &back), SH_HIST_VALID);
    CHECK_EQ(back.peak_min, SH_HIST_NO_PEAK);
}

static void test_ring(void)
{
    sh_hist_t h;
    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(s_erases, 0, sizeof(s_erases));
    CHECK(sh_hist_open(&h, &FLASH));
    CHECK_EQ(h.head, 0);
    CHECK_EQ(walk(&h, true), 0);

    /* records in order; newest / oldest first */
    for (uint16_t d = 100; d < 110; d++) {
        const sh_hist_rec_t r = rec(d);
        CHECK(sh_hist_append(&h, &r));
    }
    CHECK_EQ(walk(&h, true), 10);
    CHECK_EQ(s_days.day[0], 109);
    CHECK_EQ(s_days.day[9], 100);
    CHECK_EQ(walk(&h, false), 10);
    CHECK_EQ(s_days.day[0], 100);
    CHECK_EQ(sh_hist_walk(&h, true, 3, take_day, &s_days), 3);       /* at most `max` */

    /* after a restart the newest is found again */
    sh_hist_t h2;
    CHECK(sh_hist_open(&h2, &FLASH));
    CHECK_EQ(h2.head, 10);
    const sh_hist_rec_t r110 = rec(110);
    CHECK(sh_hist_append(&h2, &r110));
    CHECK_EQ(walk(&h2, true), 11);
    CHECK_EQ(s_days.day[0], 110);

    /* overflow: the oldest sector is overwritten, an empty record is always left after the newest */
    for (uint16_t d = 111; d < 100 + 5000; d++) {
        const sh_hist_rec_t r = rec(d);
        sh_hist_append(&h2, &r);
    }
    const unsigned kept = walk(&h2, false);
    CHECK(kept >= 2048 - 128 && kept <= 2047);
    CHECK_EQ(s_days.day[kept - 1], 100 + 5000 - 1);
    int order = 0;
    for (unsigned i = 1; i < kept; i++) {
        order += s_days.day[i] != s_days.day[i - 1] + 1;
    }
    CHECK_EQ(order, 0);
    unsigned emax = 0, emin = 1000;
    for (int s = 0; s < FLASH_SIZE / SECTOR; s++) {
        emax = s_erases[s] > emax ? s_erases[s] : emax;
        emin = s_erases[s] < emin ? s_erases[s] : emin;
    }
    CHECK(emax - emin <= 1);                                /* the wear is even */
    sh_hist_t h3;
    CHECK(sh_hist_open(&h3, &FLASH));
    CHECK_EQ(h3.head, h2.head);
    CHECK_EQ(walk(&h3, true), kept);

    /* a damaged record (power cut while writing) keeps its place and is skipped */
    const uint32_t newest = (h3.head + h3.slots - 1) % h3.slots;
    s_flash[newest * SH_HIST_REC_SIZE + 5] ^= 0x10;
    sh_hist_t h4;
    CHECK(sh_hist_open(&h4, &FLASH));
    CHECK_EQ(h4.head, h3.head);
    CHECK_EQ(walk(&h4, true), kept - 1);
    CHECK_EQ(s_days.day[0], 100 + 5000 - 2);
    const sh_hist_rec_t r9 = rec(9000);
    CHECK(sh_hist_append(&h4, &r9));
    CHECK_EQ(walk(&h4, true), kept);
    CHECK_EQ(s_days.day[0], 9000);
    CHECK_EQ(s_days.day[1], 100 + 5000 - 2);

    /* a power cut after the next sector was erased, before the record was written */
    memset(s_flash, 0xFF, sizeof(s_flash));
    sh_hist_t h5;
    CHECK(sh_hist_open(&h5, &FLASH));
    for (uint16_t d = 0; d < 127; d++) {
        const sh_hist_rec_t r = rec(d);
        sh_hist_append(&h5, &r);
    }
    s_fail_write = true;                                    /* record 127: the erase only */
    const sh_hist_rec_t r127 = rec(127);
    CHECK(!sh_hist_append(&h5, &r127));
    s_fail_write = false;
    sh_hist_t h6;
    CHECK(sh_hist_open(&h6, &FLASH));
    CHECK_EQ(h6.head, 127);
    CHECK_EQ(walk(&h6, true), 127);

    /* an area that was never used (unknown content, no empty record) is erased */
    memset(s_flash, 0x5A, sizeof(s_flash));
    sh_hist_t h7;
    CHECK(sh_hist_open(&h7, &FLASH));
    CHECK_EQ(walk(&h7, true), 0);
    CHECK_EQ(s_flash[1234], 0xFF);
    /* garbage with empty places: the records start after it; a write over it erases its sector */
    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(s_flash + 100, 0x00, 40);
    sh_hist_t h8;
    CHECK(sh_hist_open(&h8, &FLASH));
    CHECK_EQ(h8.head, 5);                                   /* after the damaged records 3, 4 */
    const sh_hist_rec_t r1 = rec(1);
    CHECK(sh_hist_append(&h8, &r1));
    CHECK_EQ(walk(&h8, true), 1);

    /* reset of the history */
    CHECK(sh_hist_clear(&h8));
    CHECK_EQ(walk(&h8, true), 0);
    sh_hist_t h9;
    CHECK(sh_hist_open(&h9, &FLASH));
    CHECK_EQ(h9.head, 0);
}

/* Faults found by the other experiment variant (step 9, PR #35), on this ring. */
static void test_ring_faults(void)
{
    sh_hist_t h, again;

    /* a free place ahead of the head that lost a bit: the records next to it must stay */
    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(s_erases, 0, sizeof(s_erases));
    CHECK(sh_hist_open(&h, &FLASH));
    for (uint16_t d = 1; d <= 300; d++) {
        const sh_hist_rec_t r = rec(d);
        CHECK(sh_hist_append(&h, &r));
    }
    s_flash[310 * SH_HIST_REC_SIZE + 3] = 0xFE;
    memset(s_erases, 0, sizeof(s_erases));               /* the normal erases of the filling are done */
    CHECK(sh_hist_open(&again, &FLASH));
    CHECK_EQ(again.head, 300);
    for (uint16_t d = 301; d <= 320; d++) {
        const sh_hist_rec_t r = rec(d);
        sh_hist_append(&again, &r);
    }
    CHECK_EQ(walk(&again, false), 320);                  /* nothing lost, the damaged place skipped */
    CHECK_EQ(s_days.day[0], 1);
    CHECK_EQ(s_days.day[319], 320);
    CHECK_EQ(s_erases[2], 0);                            /* its sector (records 256 - 383) kept */

    /* the power failed in the middle of a record (13 bytes written): skipped, the next one follows */
    uint8_t b[SH_HIST_REC_SIZE];
    const sh_hist_rec_t r321 = rec(321);
    sh_hist_encode(&r321, b);
    const uint32_t at = again.head;
    memcpy(s_flash + at * SH_HIST_REC_SIZE, b, 13);
    CHECK(sh_hist_open(&h, &FLASH));
    CHECK_EQ(h.head, at + 1);
    const sh_hist_rec_t r322 = rec(322);
    CHECK(sh_hist_append(&h, &r322));
    CHECK_EQ(walk(&h, true), 321);
    CHECK_EQ(s_days.day[0], 322);
    CHECK_EQ(s_days.day[1], 320);

    /* years on, with a restart after every record: the restart finds what the running firmware has */
    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(s_erases, 0, sizeof(s_erases));
    CHECK(sh_hist_open(&h, &FLASH));
    int bad = 0;
    for (uint32_t d = 1; d < 3 * 2048 + 77; d++) {
        const sh_hist_rec_t r = rec((uint16_t)d);
        bad += !sh_hist_append(&h, &r);
        if (!sh_hist_open(&again, &FLASH) || again.head != h.head) {
            bad++;
            break;
        }
    }
    CHECK_EQ(bad, 0);
    const unsigned kept = walk(&h, true);
    CHECK(kept >= 2048 - 128 && kept <= 2047);
    CHECK_EQ(s_days.day[0], 3 * 2048 + 76);
}

static void test_csv(void)
{
    CHECK(strcmp(sh_hist_csv_header,
                 "date;collector_max;collector_min;tank_max;tank_min;pump_charging_min;pump_protection_min;"
                 "pump_other_min;night_cooling_min;heater_min;alarms;collector_peak;clock_reliable;partial_day;"
                 "sunrise;sunset;cooling_target\r\n") == 0);
    sh_hist_rec_t r;
    sh_hist_rec_init(&r, sh_day_number(2026, 7, 5));
    r.col_max = 1153;
    r.col_min = -52;
    r.tank_max = 611;
    r.tank_min = 400;
    r.run_min[0] = 190;
    r.run_min[1] = 4;
    r.alarms = SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_HOT);
    r.peak_min = 14 * 60 + 5;
    r.clock_reliable = true;
    char buf[SH_HIST_CSV_LINE_MAX];
    CHECK(sh_hist_csv_line(buf, sizeof(buf), &r) > 0);
    CHECK(strcmp(buf, "2026-07-05;115.3;-5.2;61.1;40.0;190;4;0;0;0;E1 Hot;14:05;1;0;;;\r\n") == 0);

    /* no data: empty fields; a partial day */
    sh_hist_rec_init(&r, 0);
    r.partial = true;
    CHECK(sh_hist_csv_line(buf, sizeof(buf), &r) > 0);
    CHECK(strcmp(buf, "2020-01-01;;;;;0;0;0;0;0;;;0;1;;;\r\n") == 0);
    /* the learned values, when there are (groups C / D) */
    r.sunrise = 6 * 60 + 2;
    r.sunset = 20 * 60 + 41;
    r.cool_target = 55;
    CHECK(sh_hist_csv_line(buf, sizeof(buf), &r) > 0);
    CHECK(strcmp(buf, "2020-01-01;;;;;0;0;0;0;0;;;0;1;06:02;20:41;55\r\n") == 0);

    /* the longest line fits */
    sh_hist_rec_init(&r, 0xFFFE);
    r.col_max = r.col_min = r.tank_max = r.tank_min = -32767;
    r.run_min[0] = r.run_min[1] = r.run_min[2] = r.cool_min = r.heater_min = 65535;
    r.alarms = 0xFFFF;
    r.peak_min = 1439;
    r.sunrise = r.sunset = 1439;
    r.cool_target = 254;
    const int len = sh_hist_csv_line(buf, sizeof(buf), &r);
    CHECK(len > 0 && len < SH_HIST_CSV_LINE_MAX);
    CHECK_EQ(sh_hist_csv_line(buf, 20, &r), -1);
}

int main(void)
{
    test_browser();
    test_trust();
    test_peak();
    test_dates();
    test_tz();
    test_kinds();
    test_chart_clock();
    test_chart_no_clock();
    test_summary_day();
    test_summary_group_c();
    test_clock_jumps();
    test_record();
    test_ring();
    test_ring_faults();
    test_csv();
    return test_summary("test_clock_history");
}
