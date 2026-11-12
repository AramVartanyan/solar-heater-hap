/* Host test of the clock rules (main/sh_clock.c; doc/tasks/step9-clock-history.md 3.1): when the
 * time of the browser is taken, the time zone list, the dates, the time zone check by the
 * collector peak. */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test_util.h"
#include "sh_clock.h"

/* The browser sets the clock only if SNTP did not during the last 24 h. */
static void test_browser(void)
{
    CHECK(sh_clock_browser_ok(false, 0));               /* never set by SNTP: AP mode, no internet */
    CHECK(sh_clock_browser_ok(false, 100000));
    CHECK(!sh_clock_browser_ok(true, 0));               /* just set by SNTP */
    CHECK(!sh_clock_browser_ok(true, 12 * 3600));
    CHECK(!sh_clock_browser_ok(true, 24 * 3600 - 1));
    CHECK(sh_clock_browser_ok(true, 24 * 3600));        /* SNTP silent for 24 h: internet lost */
    CHECK(sh_clock_browser_ok(true, 30 * 3600));
    CHECK_EQ(SH_CLOCK_SNTP_PERIOD_S, 12 * 3600);
}

/* Local time of `utc` ("YYYY-MM-DD hh:mm") with the rule of a zone: hours * 100 + minutes. */
static int local_hhmm(const char *zone, const char *utc)
{
    int y, mo, d, h, mi;
    CHECK_EQ(sscanf(utc, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi), 5);
    const time_t t = ((time_t)sh_clock_day(y, mo, d) + 18262) * 86400 + h * 3600 + mi * 60;
    struct tm tm;
    const int z = sh_zone_find(zone);
    CHECK(z != SH_ZONE_NONE);
    setenv("TZ", z == SH_ZONE_NONE ? "UTC0" : sh_zones[z].tz, 1);
    tzset();
    localtime_r(&t, &tm);
    return tm.tm_hour * 100 + tm.tm_min;
}

static void test_zones(void)
{
    CHECK_EQ(sh_zone_find("UTC"), 0);
    CHECK_EQ(sh_zone_find("Europe/Nowhere"), SH_ZONE_NONE);
    CHECK_EQ(sh_zone_find(NULL), SH_ZONE_NONE);
    CHECK_EQ(sh_zone_find(""), SH_ZONE_NONE);
    for (int i = 0; i < sh_zone_count; i++) {
        CHECK_EQ(sh_zone_find(sh_zones[i].name), i);    /* every name once */
        CHECK(strlen(sh_zones[i].name) < 32 && sh_zones[i].tz[0] != '\0');
        if (i > 1) {                                    /* UTC, then by name */
            CHECK(strcmp(sh_zones[i - 1].name, sh_zones[i].name) < 0);
        }
    }

    /* the rules, with the daylight saving time */
    CHECK_EQ(local_hhmm("UTC", "2026-07-01 12:00"), 1200);
    CHECK_EQ(local_hhmm("Europe/Sofia", "2026-01-15 12:00"), 1400);
    CHECK_EQ(local_hhmm("Europe/Sofia", "2026-07-15 12:00"), 1500);
    CHECK_EQ(local_hhmm("Europe/Sofia", "2026-03-29 00:59"), 259);     /* the last minute of winter */
    CHECK_EQ(local_hhmm("Europe/Sofia", "2026-03-29 01:00"), 400);
    CHECK_EQ(local_hhmm("Europe/Sofia", "2026-10-25 00:59"), 359);
    CHECK_EQ(local_hhmm("Europe/Sofia", "2026-10-25 01:00"), 300);
    CHECK_EQ(local_hhmm("Europe/Berlin", "2026-07-15 12:00"), 1400);
    CHECK_EQ(local_hhmm("Europe/Berlin", "2026-12-15 12:00"), 1300);
    CHECK_EQ(local_hhmm("Europe/London", "2026-07-15 12:00"), 1300);
    CHECK_EQ(local_hhmm("Europe/London", "2026-12-15 12:00"), 1200);
    CHECK_EQ(local_hhmm("Europe/Istanbul", "2026-07-15 12:00"), 1500);
    CHECK_EQ(local_hhmm("Europe/Moscow", "2026-01-15 12:00"), 1500);
    CHECK_EQ(local_hhmm("America/New_York", "2026-07-15 12:00"), 800);
    CHECK_EQ(local_hhmm("America/New_York", "2026-01-15 12:00"), 700);
    CHECK_EQ(local_hhmm("America/Los_Angeles", "2026-07-15 12:00"), 500);
    CHECK_EQ(local_hhmm("Asia/Kolkata", "2026-07-15 12:00"), 1730);
    CHECK_EQ(local_hhmm("Asia/Tehran", "2026-07-15 12:00"), 1530);
    CHECK_EQ(local_hhmm("Asia/Tokyo", "2026-07-15 12:00"), 2100);
    CHECK_EQ(local_hhmm("Australia/Sydney", "2026-01-15 12:00"), 2300);
    CHECK_EQ(local_hhmm("Australia/Sydney", "2026-07-15 12:00"), 2200);
    CHECK_EQ(local_hhmm("Pacific/Auckland", "2026-01-15 12:00"), 100);
    CHECK_EQ(local_hhmm("America/Sao_Paulo", "2026-07-15 12:00"), 900);
}

static void test_dates(void)
{
    CHECK_EQ(sh_clock_day(2020, 1, 1), 0);
    CHECK_EQ(sh_clock_day(2020, 1, 2), 1);
    CHECK_EQ(sh_clock_day(2020, 2, 29), 59);            /* leap year */
    CHECK_EQ(sh_clock_day(2020, 3, 1), 60);
    CHECK_EQ(sh_clock_day(2020, 12, 31), 365);
    CHECK_EQ(sh_clock_day(2021, 1, 1), 366);
    CHECK_EQ(sh_clock_day(2026, 11, 12), 2507);
    CHECK_EQ(sh_clock_day(2100, 3, 1), 29279);          /* 2100 is not a leap year */
    CHECK_EQ(sh_clock_day(2019, 12, 31), 0);            /* before the first day: a clock not set */
    CHECK_EQ(sh_clock_day(1970, 1, 1), 0);

    int y, m, d, prev = -1;
    for (int n = 0; n <= 65535; n++) {                  /* every day there and back, in order */
        sh_clock_date((uint16_t)n, &y, &m, &d);
        if (sh_clock_day(y, m, d) != n || m < 1 || m > 12 || d < 1 || d > 31) {
            CHECK_EQ(sh_clock_day(y, m, d), n);
            break;
        }
        const int key = y * 10000 + m * 100 + d;
        if (key <= prev) {
            CHECK(key > prev);
            break;
        }
        prev = key;
    }
    sh_clock_date(0, &y, &m, &d);
    CHECK(y == 2020 && m == 1 && d == 1);
    sh_clock_date(59, &y, &m, &d);
    CHECK(y == 2020 && m == 2 && d == 29);
    sh_clock_date(2507, &y, &m, &d);
    CHECK(y == 2026 && m == 11 && d == 12);
}

#define HM(h, m)        ((uint16_t)((h) * 60 + (m)))

static void sunny(sh_peak_t *p, uint16_t peak_min)
{
    sh_peak_day(p, 650, 80, peak_min);
}

static void cloudy(sh_peak_t *p, uint16_t peak_min)
{
    sh_peak_day(p, 279, 80, peak_min);                  /* 19.9 K above the minimum */
}

/* The window 10:00 - 17:00, to the minute. */
static void test_peak_window(void)
{
    sh_peak_t p;
    const struct {
        uint16_t min;
        bool in;
    } t[] = {
        { HM(9, 59), false }, { HM(10, 0), true }, { HM(10, 1), true }, { HM(13, 10), true },
        { HM(16, 59), true }, { HM(17, 0), true }, { HM(17, 1), false },
        { HM(0, 0), false }, { HM(23, 59), false }, { HM(20, 30), false },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        sh_peak_reset(&p, true);
        sunny(&p, t[i].min);
        CHECK_EQ(p.last_min, t[i].min);
        CHECK_EQ(p.last_in, t[i].in);
        CHECK_EQ(p.in_row, t[i].in ? 1 : 0);
        CHECK_EQ(p.out_row, t[i].in ? 0 : 1);
        CHECK(p.reliable);                              /* one day changes nothing */
        sunny(&p, t[i].min);
        CHECK_EQ(p.reliable, t[i].in);                  /* the second one in a row does */
    }

    /* a sunny day: the collector at least 20 K above its minimum of the day */
    sh_peak_reset(&p, true);
    sh_peak_day(&p, 280, 80, HM(20, 0));
    CHECK_EQ(p.out_row, 1);
    sh_peak_day(&p, 279, 80, HM(20, 0));
    CHECK_EQ(p.out_row, 1);
    sh_peak_day(&p, -20, -220, HM(20, 0));              /* a frosty sunny day */
    CHECK(!p.reliable);
    sh_peak_reset(&p, true);
    sh_peak_day(&p, 650, 80, SH_PEAK_NONE);             /* no collector value that day */
    CHECK_EQ(p.out_row + p.in_row, 0);
    CHECK_EQ(p.last_min, SH_PEAK_NONE);
}

/* 2 sunny days in a row outside -> unreliable; 2 in a row inside -> reliable again; cloudy days
 * neither count nor break the row. */
static void test_peak_rows(void)
{
    sh_peak_t p;

    /* a set clock with a confirmed zone is reliable at once; without it — after 2 good days */
    sh_peak_reset(&p, true);
    CHECK(p.reliable);
    CHECK_EQ(p.last_min, SH_PEAK_NONE);
    sh_peak_reset(&p, false);
    CHECK(!p.reliable);
    sunny(&p, HM(12, 30));
    CHECK(!p.reliable);
    sunny(&p, HM(12, 40));
    CHECK(p.reliable);

    /* a wrong zone: the peak in the evening */
    sh_peak_reset(&p, true);
    sunny(&p, HM(21, 0));
    CHECK(p.reliable);
    sunny(&p, HM(21, 10));
    CHECK(!p.reliable);
    sunny(&p, HM(21, 5));
    CHECK(!p.reliable);
    sunny(&p, HM(13, 0));                               /* one good day is not enough */
    CHECK(!p.reliable);
    sunny(&p, HM(13, 20));
    CHECK(p.reliable);

    /* a day inside breaks the row of the days outside, and the other way round */
    sh_peak_reset(&p, true);
    sunny(&p, HM(18, 0));
    sunny(&p, HM(13, 0));
    sunny(&p, HM(18, 0));
    CHECK(p.reliable);
    sunny(&p, HM(18, 0));
    CHECK(!p.reliable);
    sunny(&p, HM(13, 0));
    sunny(&p, HM(18, 0));
    sunny(&p, HM(13, 0));
    CHECK(!p.reliable);
    sunny(&p, HM(13, 0));
    CHECK(p.reliable);

    /* cloudy days: not counted, the row goes on over them */
    sh_peak_reset(&p, true);
    cloudy(&p, HM(21, 0));
    cloudy(&p, HM(21, 0));
    cloudy(&p, HM(21, 0));
    CHECK(p.reliable);
    CHECK_EQ(p.last_min, SH_PEAK_NONE);
    sunny(&p, HM(21, 0));
    cloudy(&p, HM(13, 0));
    cloudy(&p, HM(13, 0));
    CHECK(p.reliable);
    sunny(&p, HM(21, 0));
    CHECK(!p.reliable);
    sunny(&p, HM(13, 0));
    cloudy(&p, HM(21, 0));
    CHECK_EQ(p.last_min, HM(13, 0));                    /* the page shows the last sunny day */
    CHECK(p.last_in);
    cloudy(&p, HM(21, 0));
    CHECK(!p.reliable);
    sunny(&p, HM(13, 0));
    CHECK(p.reliable);

    /* a new clock, a new zone, the reset of the learned values: from the start */
    sunny(&p, HM(21, 0));
    sh_peak_reset(&p, true);
    CHECK(p.reliable && p.out_row == 0 && p.in_row == 0);
    sunny(&p, HM(21, 0));
    CHECK(p.reliable);
}

int main(void)
{
    test_browser();
    test_zones();
    test_dates();
    test_peak_window();
    test_peak_rows();
    return test_summary("test_clock");
}
