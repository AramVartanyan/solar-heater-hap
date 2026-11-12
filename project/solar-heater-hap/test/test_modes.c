/* Host test of the modes of group C (doc/tasks/step10-cooling-vacation-seasons.md 3.2 - 3.4, 3.7):
 * the sunrise and sunset (main/sh_sun.c), the seasons (main/sh_season.c), the night cooling and the
 * vacation with the learning (main/sh_cool.c) and how they are put together (main/sh_modes.c).
 * The times of the triggers and of the end are checked at ±1 min. Temperatures are tenths of °C,
 * times seconds, local minutes of the day. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_sun.h"
#include "sh_season.h"
#include "sh_cool.h"
#include "sh_modes.h"

#define INV     SH_TEMP_INVALID
#define H       3600u
#define M(h, m) ((h) * 60 + (m))

/* ==== sunrise, sunset ===================================================================== */

typedef struct {
    sh_sun_t s;
    sh_sun_in_t in;
    uint32_t now;
    uint8_t evt;                /* all events since the last look */
} sun_t;

static sun_t sun(bool clock)
{
    sun_t u;
    memset(&u, 0, sizeof(u));
    sh_sun_init(&u.s);
    u.in.clock = clock;
    u.in.day = 2500;
    u.in.min = 0;
    u.now = 1000;
    return u;
}

/* `n` steps of 60 s; the local minute moves with them */
static void sun_run(sun_t *u, int n)
{
    while (n-- > 0) {
        u->now += 60;
        if (u->in.clock && ++u->in.min >= 1440) {
            u->in.min = 0;
            u->in.day++;
        }
        u->evt |= sh_sun_step(&u->s, &u->in, u->now);
    }
}

static uint8_t sun_evt(sun_t *u)
{
    const uint8_t e = u->evt;
    u->evt = 0;
    return e;
}

static void test_sunrise(void)
{
    /* with a clock: armed after the midnight; the collector rises 2 K in 10 min with the pump off */
    sun_t u = sun(true);
    u.in.min = M(23, 0);
    u.in.t_col = 50;
    u.in.t_tank = 400;
    sun_run(&u, 70);                        /* over the midnight */
    CHECK(sun_evt(&u) & SH_SUN_EVT_DAY);
    CHECK(u.s.armed);
    u.in.min = M(6, 0);
    sun_run(&u, 11);                        /* 06:11, 11 flat samples */
    sun_evt(&u);
    /* 1.9 K against the sample 10 min ago: not yet */
    u.in.t_col = 69;
    sun_run(&u, 1);
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_RISE));
    /* 2.0 K: the sunrise, at this minute */
    u.in.t_col = 70;
    sun_run(&u, 1);                         /* 06:13 */
    CHECK(sun_evt(&u) & SH_SUN_EVT_RISE);
    CHECK(!u.s.armed);
    CHECK_EQ(u.s.rise_min, M(6, 13));
    const int16_t rise = u.s.rise_min;
    /* only once a day */
    for (int i = 0; i < 30; i++) {
        u.in.t_col = (int16_t)(u.in.t_col + 10);
        sun_run(&u, 1);
    }
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_RISE));
    CHECK_EQ(u.s.rise_min, rise);

    /* exactly 2.0 K in exactly 10 min (11 samples a minute apart), the pump off all the time */
    u = sun(true);
    u.in.min = M(23, 59);
    u.in.t_col = 100;
    u.in.t_tank = 400;
    sun_run(&u, 2);
    sun_evt(&u);
    u.in.min = M(5, 0);
    u.in.t_col = 100;
    sun_run(&u, 11);                        /* a flat start: 11 samples of 10.0 */
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_RISE));
    u.in.t_col = 120;                       /* +2.0 K against the sample 10 min ago */
    sun_run(&u, 1);
    CHECK(sun_evt(&u) & SH_SUN_EVT_RISE);

    /* a running pump: no samples; after it stops 10 min are needed again */
    u = sun(true);
    u.in.min = M(23, 59);
    u.in.t_col = 100;
    u.in.t_tank = 400;
    sun_run(&u, 2);
    u.in.min = M(5, 0);
    sun_run(&u, 5);
    u.in.pump_on = true;
    u.in.t_col = 200;
    sun_run(&u, 3);
    u.in.pump_on = false;
    sun_run(&u, 10);                        /* 10 samples since the stop: not a full window */
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_RISE));
    u.in.t_col = 220;
    sun_run(&u, 1);
    CHECK(sun_evt(&u) & SH_SUN_EVT_RISE);

    /* the clock set during the day (not at a midnight): looked for only before noon */
    u = sun(true);
    u.in.min = M(13, 0);
    u.in.t_col = 300;
    u.in.t_tank = 600;
    sun_run(&u, 11);
    u.in.t_col = 330;
    sun_run(&u, 1);
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_RISE));
    u = sun(true);
    u.in.min = M(11, 48);                   /* the first step at 11:49 */
    u.in.t_col = 300;
    u.in.t_tank = 600;
    sun_run(&u, 11);
    u.in.t_col = 330;
    sun_run(&u, 1);
    CHECK(sun_evt(&u) & SH_SUN_EVT_RISE);

    /* without a clock: only after a sunset */
    u = sun(false);
    u.in.t_col = 100;
    u.in.t_tank = 400;
    sun_run(&u, 11);
    u.in.t_col = 130;
    sun_run(&u, 1);
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_RISE));
    CHECK_EQ(u.s.rise_min, SH_SUN_NONE);
}

static void test_sunset(void)
{
    sun_t u = sun(true);
    u.in.min = M(12, 0);
    u.in.t_tank = 500;
    u.in.t_col = 450;
    sun_run(&u, 1);                         /* the collector never above the tank: no sunset */
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_SET));
    u.in.t_col = 501;                       /* above */
    u.in.pump_on = true;
    sun_run(&u, 60);
    u.in.t_col = 499;                       /* below, but the pump runs */
    sun_run(&u, 1);
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_SET));
    u.in.pump_on = false;
    u.in.t_col = 500;                       /* equal: not below */
    sun_run(&u, 1);
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_SET));
    u.in.min = M(14, 59);
    u.in.t_col = 499;
    sun_run(&u, 1);                         /* 15:00 */
    CHECK(sun_evt(&u) & SH_SUN_EVT_SET);
    CHECK(u.s.night);
    CHECK_EQ(u.s.set_min, M(15, 0));
    const uint32_t first = u.s.sunset_s;
    /* a cloud: the collector above the tank again (the pump off) -> day again; the last fall counts */
    u.in.t_col = 520;
    sun_run(&u, 1);
    CHECK(!u.s.night);
    u.in.min = M(18, 29);
    u.in.t_col = 480;
    sun_run(&u, 1);
    CHECK(sun_evt(&u) & SH_SUN_EVT_SET);
    CHECK_EQ(u.s.set_min, M(18, 30));
    CHECK(u.s.sunset_s > first);
    /* falling further: no new sunset */
    u.in.t_col = 300;
    sun_run(&u, 60);
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_SET));
    /* the midnight: the day's values are handed over, the new day has none */
    u.in.min = M(23, 58);
    sun_run(&u, 3);
    CHECK(sun_evt(&u) & SH_SUN_EVT_DAY);
    CHECK_EQ(u.s.done_set, M(18, 30));
    CHECK_EQ(u.s.done_rise, SH_SUN_NONE);
    CHECK_EQ(u.s.set_min, SH_SUN_NONE);
    CHECK(u.s.night);                       /* the night goes on over the midnight */

    /* without a clock: the sunset arms the sunrise; no minutes */
    u = sun(false);
    u.in.t_tank = 500;
    u.in.t_col = 600;
    sun_run(&u, 10);
    u.in.t_col = 490;
    sun_run(&u, 1);
    CHECK(sun_evt(&u) & SH_SUN_EVT_SET);
    CHECK_EQ(u.s.set_min, SH_SUN_NONE);
    CHECK(u.s.armed);
    sun_run(&u, 600);
    u.in.t_col = 520;                       /* the morning: rises 3 K, above the tank later */
    sun_run(&u, 1);
    CHECK(sun_evt(&u) & SH_SUN_EVT_RISE);
    CHECK(!u.s.night);

    /* a faulty sensor: nothing detected */
    u = sun(true);
    u.in.min = M(12, 0);
    u.in.t_tank = INV;
    u.in.t_col = 600;
    sun_run(&u, 10);
    u.in.t_col = 400;
    sun_run(&u, 1);
    CHECK(!(sun_evt(&u) & SH_SUN_EVT_SET));
}

static void test_sun_list(void)
{
    sh_sun_list_t l;
    memset(&l, 0, sizeof(l));
    CHECK_EQ(sh_sun_list_avg(&l), SH_SUN_NONE);
    sh_sun_list_add(&l, SH_SUN_NONE);       /* a day without a value is not added */
    CHECK_EQ(l.n, 0);
    sh_sun_list_add(&l, 400);
    CHECK_EQ(sh_sun_list_avg(&l), 400);
    sh_sun_list_add(&l, 403);
    CHECK_EQ(sh_sun_list_avg(&l), 402);     /* 401.5 rounded */
    for (int i = 0; i < 7; i++) {
        sh_sun_list_add(&l, (int16_t)(500 + i));
    }
    CHECK_EQ(l.n, 7);                       /* the last 7 */
    CHECK_EQ(l.v[0], 506);
    CHECK_EQ(sh_sun_list_avg(&l), 503);
}

/* ==== seasons ============================================================================= */

/* One day: the lowest collector `min`, and a charging run of `run_s` with the collector at `hot`. */
static bool season_day(sh_season_t *s, int16_t min, uint32_t run_s, int16_t hot, uint32_t *now)
{
    sh_season_observe(s, min, false, *now);
    *now += 600;
    if (run_s) {
        sh_season_observe(s, hot, true, *now);
        *now += run_s;
        sh_season_observe(s, hot, true, *now);
        sh_season_observe(s, hot, false, *now + 1);
    }
    *now += 86400;
    return sh_season_day(s);
}

static void test_season_switch(void)
{
    uint32_t now = 0;
    sh_season_t s;
    sh_season_init(&s, false);

    /* summer -> winter: below 5 °C, 3 days in a row; 5.0 °C does not count */
    CHECK(!season_day(&s, 49, 0, 0, &now));
    CHECK(!season_day(&s, 49, 0, 0, &now));
    CHECK(!season_day(&s, 50, 0, 0, &now));         /* the series is broken */
    CHECK(!season_day(&s, 49, 0, 0, &now));
    CHECK(!season_day(&s, INV, 0, 0, &now));        /* no value: neither counted nor broken */
    CHECK(!season_day(&s, -20, 0, 0, &now));
    CHECK(!s.winter);
    CHECK(season_day(&s, 0, 0, 0, &now));
    CHECK(s.winter);
    /* winter: cold days do nothing; the hot days only from the winter */
    CHECK(!season_day(&s, -50, 0, 0, &now));

    /* winter -> summer: over 60 °C while charging without a break for over 1 h, 3 days */
    CHECK(!season_day(&s, 100, H, 601, &now));      /* exactly 1 h: not over */
    CHECK(!season_day(&s, 100, H + 1, 600, &now));  /* 60.0 °C: not above */
    CHECK(!season_day(&s, 100, H + 1, 601, &now));
    CHECK(!season_day(&s, 100, H + 1, 601, &now));
    CHECK(!season_day(&s, 100, 0, 0, &now));        /* broken */
    CHECK(!season_day(&s, 100, H + 1, 601, &now));
    CHECK(!season_day(&s, 100, H + 1, 601, &now));
    CHECK(season_day(&s, 100, H + 1, 601, &now));
    CHECK(!s.winter);

    /* a break of the charging starts the hour again */
    sh_season_init(&s, true);
    sh_season_observe(&s, 700, true, 0);
    sh_season_observe(&s, 700, false, 1800);
    sh_season_observe(&s, 700, true, 1801);
    sh_season_observe(&s, 700, true, 1801 + H);
    CHECK(!s.hot);
    sh_season_observe(&s, 700, true, 1802 + H);
    CHECK(s.hot);

    /* by the daylight saving time */
    sh_season_init(&s, false);
    CHECK(!sh_season_by_dst(&s, true));
    CHECK(sh_season_by_dst(&s, false));
    CHECK(s.winter);
    CHECK(sh_season_by_dst(&s, true));
    CHECK(!s.winter);
}

/* ==== night cooling and vacation (sh_cool) =============================================== */

typedef struct {
    sh_cool_t c;
    sh_cool_learn_t l;
    sh_cool_in_t in;
    uint32_t now;
} cool_t;

/* A reliable clock at `min`, tank above Tmax (75 °C, target 63 °C), collector 15 °C, summer. */
static cool_t cool(int min)
{
    cool_t u;
    memset(&u, 0, sizeof(u));
    u.c.last_min = -1;
    u.c.target = INV;
    u.in.clock = true;
    u.in.min = (uint16_t)min;
    u.in.avg_rise = u.in.avg_set = SH_SUN_NONE;
    u.in.t_col = 150;
    u.in.t_tank = 750;
    u.in.tank_max = true;
    u.in.night_cool = true;
    u.in.target = 630;
    u.now = 100000;
    sh_cool_step(&u.c, &u.l, &u.in, u.now);
    return u;
}

/* `n` steps of a minute */
static uint8_t cool_run(cool_t *u, int n)
{
    uint8_t e = 0;
    while (n-- > 0) {
        u->now += 60;
        if (u->in.clock) {
            u->in.min = (uint16_t)((u->in.min + 1) % 1440);
        }
        sh_cool_step(&u->c, &u->l, &u->in, u->now);
        e |= u->c.events;
        u->in.sunrise = u->in.sunset = false;
    }
    return e;
}

static void test_trigger_a(void)
{
    /* tank above Tmax: from 04:00 (03:59 not) */
    cool_t u = cool(M(3, 58));
    CHECK_EQ(cool_run(&u, 1), 0);           /* 03:59 */
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_START);   /* 04:00 */
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
    CHECK_EQ(u.c.target, 630);
    CHECK_EQ(u.c.until, M(7, 0));
    /* the end at 07:00 (06:59 still on) */
    CHECK_EQ(cool_run(&u, 178), 0);         /* 06:58 */
    cool_run(&u, 1);                        /* 06:59 */
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_END);     /* 07:00 */
    CHECK_EQ(u.c.end, SH_COOL_END_MORNING);
    CHECK_EQ(u.c.ended, SH_COOL_TMAX);
    CHECK(u.c.done);

    /* the switch of the season off: no a) */
    u = cool(M(3, 58));
    u.in.night_cool = false;
    cool_run(&u, 10);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    /* not above Tmax: no a) */
    u = cool(M(3, 58));
    u.in.tank_max = false;
    cool_run(&u, 10);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    /* the tank above Tmax only later in the window: starts then */
    u.in.tank_max = true;
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
    /* the tank already at the target: no session */
    u = cool(M(3, 58));
    u.in.t_tank = 630;
    cool_run(&u, 5);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    /* in the evening: no a) (the window is 04:00 - 07:00) */
    u = cool(M(20, 0));
    cool_run(&u, 400);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
}

static void test_trigger_b(void)
{
    /* E4 during the day: from 03:00 (02:59 not), in both seasons (no switch needed) */
    cool_t u = cool(M(14, 0));
    u.in.tank_max = false;
    u.in.night_cool = false;
    u.in.e4 = true;
    cool_run(&u, 30);
    u.in.e4 = false;
    CHECK(u.c.e4_seen);
    u.in.min = M(2, 57);
    CHECK_EQ(cool_run(&u, 2), 0);           /* 02:59 */
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_START);   /* 03:00 */
    CHECK_EQ(u.c.why, SH_COOL_E4);
    CHECK_EQ(u.c.target, 630);
    /* the target: the end, no new start this night */
    u.in.t_tank = 631;
    CHECK_EQ(cool_run(&u, 1), 0);
    u.in.t_tank = 630;
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_END);
    CHECK_EQ(u.c.end, SH_COOL_END_TARGET);
    u.in.t_tank = 700;
    cool_run(&u, 30);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    /* the E4 of the day before is cleared at 07:00 */
    CHECK(u.c.e4_seen);
    u.in.min = M(6, 58);
    cool_run(&u, 2);
    CHECK(!u.c.e4_seen);

    /* without the E4: no b) */
    u = cool(M(2, 50));
    u.in.tank_max = false;
    cool_run(&u, 30);
    CHECK_EQ(u.c.why, SH_COOL_NONE);

    /* both: b) at 03:00 */
    u = cool(M(2, 59));
    u.in.e4 = true;
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_E4);
}

static void test_cooling_end(void)
{
    /* the sunrise ends it (also before 07:00) */
    cool_t u = cool(M(4, 30));
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
    u.in.sunrise = true;
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_END);
    CHECK_EQ(u.c.end, SH_COOL_END_SUNRISE);
    cool_run(&u, 10);
    CHECK_EQ(u.c.why, SH_COOL_NONE);        /* done for the night */
    /* a new night: 12:00 clears "done" */
    u.in.min = M(11, 58);
    cool_run(&u, 3);
    CHECK(!u.c.done);

    /* the "until": the average sunrise when it is before 07:00 */
    u = cool(M(3, 59));
    u.in.avg_rise = M(5, 40);
    cool_run(&u, 1);
    CHECK_EQ(u.c.until, M(5, 40));
    u = cool(M(3, 59));
    u.in.avg_rise = M(7, 30);
    cool_run(&u, 1);
    CHECK_EQ(u.c.until, M(7, 0));

    /* a faulty sensor ends it, without "done" */
    u = cool(M(4, 30));
    u.in.t_tank = INV;
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_END);
    CHECK_EQ(u.c.end, SH_COOL_END_SENSOR);
    CHECK(!u.c.done);
    u.in.t_tank = 750;
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
}

static void test_cooling_heater(void)
{
    /* the tank rises >= 0.5 K within 10 min in the session: stopped until the next night */
    cool_t u = cool(M(4, 0));
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
    cool_run(&u, 10);                       /* 10 samples */
    u.in.t_tank = 754;                      /* +0.4 K */
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
    u.in.t_tank = 755;                      /* +0.5 K against 10 min ago */
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_END);
    CHECK_EQ(u.c.end, SH_COOL_END_HEATER);
    CHECK(u.c.heater);
    cool_run(&u, 60);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    CHECK(u.c.heater);                      /* shown until the next night */
    u.in.sunset = true;                     /* (the sunset) */
    cool_run(&u, 1);
    CHECK(!u.c.heater);

    /* a falling tank is not a heater */
    u = cool(M(4, 0));
    for (int i = 0; i < 60; i++) {
        u.in.t_tank = (int16_t)(750 - i);
        cool_run(&u, 1);
    }
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
}

static void test_cooling_no_clock(void)
{
    /* 6 h after the detected sunset (5:59 not) */
    cool_t u = cool(0);
    u.in.clock = false;
    u.in.e4 = true;
    cool_run(&u, 1);
    u.in.e4 = false;
    u.in.night = true;
    u.in.sunset = true;
    u.in.sunset_s = u.now + 60;
    CHECK_EQ(cool_run(&u, 1), 0);           /* the sunset */
    CHECK_EQ(cool_run(&u, 359), 0);         /* 5:59 after it */
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_START);   /* 6:00 */
    CHECK_EQ(u.c.why, SH_COOL_E4);
    CHECK_EQ(u.c.until, SH_SUN_NONE);       /* until the sunrise */
    /* the end of the night (the sunrise) */
    u.in.night = false;
    u.in.sunrise = true;
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_END);
    CHECK_EQ(u.c.end, SH_COOL_END_SUNRISE);
    CHECK(!u.c.e4_seen);

    /* a) without a clock: also 6 h after the sunset */
    u = cool(0);
    u.in.clock = false;
    u.in.night = true;
    u.in.sunset_s = u.now;
    cool_run(&u, 359);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_TMAX);

    /* no sunset detected: no cooling that night */
    u = cool(0);
    u.in.clock = false;
    u.in.night = false;
    cool_run(&u, 900);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
}

static void test_vacation(void)
{
    /* with a reliable clock and the averages: from the average sunset to the average sunrise */
    cool_t u = cool(M(18, 0));
    u.in.vacation = true;
    u.in.tank_max = false;
    u.in.avg_set = M(19, 30);
    u.in.avg_rise = M(6, 10);
    cool_run(&u, 88);                       /* 19:28 */
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    cool_run(&u, 1);                        /* 19:29 */
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_START);   /* 19:30 */
    CHECK_EQ(u.c.why, SH_COOL_VACATION);
    CHECK_EQ(u.c.target, INV);              /* to the equalisation */
    CHECK_EQ(u.c.until, M(6, 10));
    CHECK(!sh_cool_vacation_night(&(sh_cool_in_t){ .clock = true, .min = M(19, 29), .avg_set = M(19, 30),
                                                   .avg_rise = M(6, 10) }));
    /* the whole night, also over the window of a) / b) (a) and b) are not used in the vacation) */
    u.in.tank_max = true;
    u.in.min = M(6, 8);
    cool_run(&u, 1);                        /* 06:09 */
    CHECK_EQ(u.c.why, SH_COOL_VACATION);
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_END);     /* 06:10 */
    CHECK_EQ(u.c.end, SH_COOL_END_SUNRISE);
    /* the day: no session */
    cool_run(&u, 600);
    CHECK_EQ(u.c.why, SH_COOL_NONE);

    /* the vacation switched off: at once */
    u = cool(M(22, 0));
    u.in.vacation = true;
    u.in.avg_set = M(19, 30);
    u.in.avg_rise = M(6, 10);
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_VACATION);
    u.in.vacation = false;
    CHECK_EQ(cool_run(&u, 1), SH_COOL_EVT_END);
    CHECK_EQ(u.c.end, SH_COOL_END_MODE);

    /* without the averages (or the clock): the night of the detection */
    u = cool(M(22, 0));
    u.in.clock = false;
    u.in.vacation = true;
    cool_run(&u, 10);
    CHECK_EQ(u.c.why, SH_COOL_NONE);
    u.in.night = true;
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_VACATION);
    u.in.night = false;
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_NONE);

    /* a running a) session gives way to the vacation */
    u = cool(M(4, 30));
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
    u.in.vacation = true;
    u.in.night = true;
    cool_run(&u, 1);
    CHECK_EQ(u.c.end, SH_COOL_END_MODE);
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_VACATION);
}

static void test_learning(void)
{
    /* E4 on the day after a night with cooling: -3 K */
    cool_t u = cool(M(3, 59));
    cool_run(&u, 1);
    CHECK_EQ(u.c.why, SH_COOL_TMAX);
    CHECK(u.c.watch);
    u.in.min = M(6, 59);
    cool_run(&u, 1);                        /* 07:00: over */
    u.in.min = M(13, 0);
    u.in.e4 = true;
    CHECK(cool_run(&u, 1) & SH_COOL_EVT_LEARNED);
    CHECK_EQ(u.l.offset, -30);
    CHECK(!u.c.watch);
    cool_run(&u, 30);                       /* the same E4: once */
    CHECK_EQ(u.l.offset, -30);
    u.in.e4 = false;

    /* E4 without a cooling night before: no change */
    u = cool(M(13, 0));
    u.in.e4 = true;
    cool_run(&u, 5);
    CHECK_EQ(u.l.offset, 0);

    /* the sunset ends the watch: an E4 on a later day does not count */
    u = cool(M(3, 59));
    cool_run(&u, 1);
    u.in.sunset = true;
    cool_run(&u, 1);
    u.in.e4 = true;
    cool_run(&u, 1);
    CHECK_EQ(u.l.offset, 0);

    /* at most -20 K */
    u = cool(M(13, 0));
    u.l.offset = -190;
    u.c.watch = true;
    u.in.e4 = true;
    cool_run(&u, 1);
    CHECK_EQ(u.l.offset, -200);

    /* 7 days in a row without E4: +1 K, never above the computed target */
    u = cool(M(13, 0));
    u.l.offset = -30;
    for (int d = 0; d < 6; d++) {
        sh_cool_day(&u.c, &u.l);
    }
    CHECK_EQ(u.l.offset, -30);
    CHECK_EQ(u.l.ok_days, 6);
    sh_cool_day(&u.c, &u.l);
    CHECK_EQ(u.l.offset, -20);
    CHECK_EQ(u.l.ok_days, 0);
    for (int d = 0; d < 14; d++) {
        sh_cool_day(&u.c, &u.l);
    }
    CHECK_EQ(u.l.offset, 0);
    for (int d = 0; d < 7; d++) {
        sh_cool_day(&u.c, &u.l);
    }
    CHECK_EQ(u.l.offset, 0);
    /* a day with E4 breaks the series */
    u.l.offset = -50;
    for (int d = 0; d < 6; d++) {
        sh_cool_day(&u.c, &u.l);
    }
    u.in.e4 = true;
    cool_run(&u, 1);
    u.in.e4 = false;
    cool_run(&u, 1);
    sh_cool_day(&u.c, &u.l);
    CHECK_EQ(u.l.ok_days, 0);
    CHECK_EQ(u.l.offset, -50);
    for (int d = 0; d < 7; d++) {
        sh_cool_day(&u.c, &u.l);
    }
    CHECK_EQ(u.l.offset, -40);
}

/* ==== put together (sh_modes) ============================================================ */

static sh_settings_t settings(void)
{
    sh_settings_t s = { .v = { SH_SSN_AUTO, 0, 70, 25, 6, 4, 300, 15, 5, 3, 1, 0 } };
    return s;
}

static const sh_installer_t INST = { .v = { [SH_INST_FLUID] = 0, [SH_INST_GLYCOL] = 0, [SH_INST_PRESSURE] = 15,
                                            [SH_INST_COL_MAX] = 120, [SH_INST_COL_TYPE] = SH_COL_FLAT,
                                            [SH_INST_AREA] = 18, [SH_INST_TANK_VOL] = 120 } };

typedef struct {
    sh_modes_t m;
    sh_settings_t set;
    sh_modes_in_t in;
    uint32_t now;
    uint8_t changed;
} modes_t;

static void modes(modes_t *u, bool clock, bool reliable)
{
    memset(u, 0, sizeof(*u));
    u->set = settings();
    sh_modes_init(&u->m, false, NULL, 0);
    u->in.set = &u->set;
    u->in.inst = &INST;
    u->in.clock_set = clock;
    u->in.reliable = reliable;
    u->in.day = 2500;
    u->in.min = M(12, 0);
    u->in.t_col = 300;
    u->in.t_tank = 500;
}

static void modes_run(modes_t *u, int n)
{
    while (n-- > 0) {
        u->now += 60;
        if (u->in.clock_set && ++u->in.min >= 1440) {
            u->in.min = 0;
            u->in.day++;
        }
        sh_modes_step(&u->m, &u->in, u->now);
        u->changed |= u->m.changed;
    }
}

/* A sunny day by the minute (the pump charges while the collector is warmer), then the night. */
static void sunny_day(modes_t *u)
{
    for (int m = 0; m < 1440; m++) {
        const int min = u->in.clock_set ? u->in.min : m;
        const bool sun = min >= M(7, 0) && min < M(18, 0);
        u->in.t_col = (int16_t)(sun ? 300 + (min < M(13, 0) ? (min - M(7, 0)) : (M(18, 0) - min)) : 150);
        u->in.pump_on = u->in.charging = u->in.t_col > u->in.t_tank + 40;
        modes_run(u, 1);
    }
}

static void test_modes_night_no_clock(void)
{
    /* without a clock: the tank above Tmax, a sunny day, the sunset, 6 h later the cooling */
    modes_t u;
    modes(&u, false, false);
    u.in.t_tank = 720;
    u.in.tank_max = true;
    u.in.t_col = 800;
    modes_run(&u, 30);                      /* the collector above the tank */
    u.in.t_col = 700;                       /* below it, the pump off: the sunset */
    modes_run(&u, 1);
    CHECK(u.m.sun.night);
    modes_run(&u, 358);
    CHECK_EQ(u.m.cool.why, SH_COOL_NONE);
    modes_run(&u, 2);
    CHECK_EQ(u.m.cool.why, SH_COOL_TMAX);
    CHECK_EQ(u.m.cool.target, 627);         /* 1.8 m² flat, 120 l */
    CHECK_EQ(u.m.target, 627);
    /* the tank cools to the target: over for the night */
    u.in.t_tank = 627;
    modes_run(&u, 1);
    CHECK_EQ(u.m.cool.why, SH_COOL_NONE);
    CHECK_EQ(u.m.cool.end, SH_COOL_END_TARGET);
}

static void test_modes_season(void)
{
    /* Aut with a reliable clock in a zone with daylight saving time: by it */
    modes_t u;
    modes(&u, true, true);
    u.in.has_dst = true;
    u.in.dst = false;
    modes_run(&u, 1);
    CHECK(u.m.winter);
    CHECK(u.changed & SH_MODES_CH_SEASON);
    CHECK(u.m.season.winter);
    u.in.dst = true;
    modes_run(&u, 1);
    CHECK(!u.m.winter);
    /* by hand: Cld / Sun, never switched by the controller */
    u.set.v[SH_SET_SEASON] = SH_SSN_WINTER;
    u.in.dst = true;
    u.changed = 0;
    modes_run(&u, 1);
    CHECK(u.m.winter);
    CHECK(!(u.changed & SH_MODES_CH_SEASON));
    u.set.v[SH_SET_SEASON] = SH_SSN_SUMMER;
    u.in.dst = false;
    modes_run(&u, 1);
    CHECK(!u.m.winter);
    CHECK(!u.m.season.winter);              /* (the recognised one untouched outside Aut) */

    /* without a clock: the observations, a day = 24 h */
    modes(&u, false, false);
    u.in.t_col = 40;                        /* cold nights */
    modes_run(&u, 3 * 1440 - 1);
    CHECK(!u.m.winter);
    modes_run(&u, 1);
    CHECK(u.m.winter);
    CHECK(u.changed & SH_MODES_CH_SEASON);
    /* the winter values of the night cooling switch: off by default */
    u.in.t_tank = 750;
    u.in.tank_max = true;
    CHECK_EQ(u.set.v[sh_settings_seasonal(SH_SET_NIGHT_COOL, u.m.winter)], 0);

    /* a reliable clock without daylight saving time: the observations, a day = the local date */
    modes(&u, true, true);
    u.in.has_dst = false;
    u.in.min = M(23, 0);
    u.in.t_col = 40;
    modes_run(&u, 60 + 1440);               /* 2 midnights (the first day is the hour before it) */
    CHECK(!u.m.winter);
    modes_run(&u, 1440);
    CHECK(u.m.winter);
}

static void test_modes_learned(void)
{
    /* with a clock: the sunrise and sunset of each day into the lists, their averages */
    modes_t u;
    modes(&u, true, true);
    u.in.min = M(0, 0);
    u.in.t_tank = 400;
    for (int d = 0; d < 3; d++) {
        sunny_day(&u);
    }
    CHECK_EQ(u.m.learned.rise.n, 3);        /* (the clock known before noon on the first day) */
    CHECK_EQ(u.m.learned.set.n, 3);
    CHECK(u.changed & SH_MODES_CH_LEARNED);
    const int16_t set = sh_sun_list_avg(&u.m.learned.set);
    CHECK_EQ(set, M(16, 22));               /* the collector falls below the tank (40.0 °C) */
    const int16_t rise = sh_sun_list_avg(&u.m.learned.rise);
    CHECK_EQ(rise, M(7, 1));                /* the jump at 07:00, seen at the next sample */

    /* the reset of the learned values */
    u.changed = 0;
    u.m.learned.cool.offset = -30;
    sh_modes_reset_learned(&u.m);
    CHECK(u.m.changed & SH_MODES_CH_LEARNED);
    CHECK_EQ(u.m.learned.rise.n, 0);
    CHECK_EQ(u.m.learned.cool.offset, 0);

    /* the learned correction moves the target */
    u.m.learned.cool.offset = -30;
    modes_run(&u, 1);
    CHECK_EQ(u.m.target, 597);

    /* the vacation night with the averages */
    sh_learned_t l;
    sh_learned_init(&l);
    sh_sun_list_add(&l.rise, M(6, 0));
    sh_sun_list_add(&l.set, M(19, 0));
    modes(&u, true, true);
    sh_modes_init(&u.m, false, &l, 0);
    u.set.v[SH_SET_VACATION] = 1;
    u.in.min = M(18, 58);
    modes_run(&u, 1);
    CHECK_EQ(u.m.cool.why, SH_COOL_NONE);
    modes_run(&u, 1);
    CHECK_EQ(u.m.cool.why, SH_COOL_VACATION);
    CHECK_EQ(u.m.cool.until, M(6, 0));
}

int main(void)
{
    test_sunrise();
    test_sunset();
    test_sun_list();
    test_season_switch();
    test_trigger_a();
    test_trigger_b();
    test_cooling_end();
    test_cooling_heater();
    test_cooling_no_clock();
    test_vacation();
    test_learning();
    test_modes_night_no_clock();
    test_modes_season();
    test_modes_learned();
    return test_summary("test_modes");
}
