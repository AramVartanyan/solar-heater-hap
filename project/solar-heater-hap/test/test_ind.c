/* Host test of the status LED indication (main/sh_ind.c; doc/tasks/step5-alarms-indication.md 3.3):
 * the colour priority, the white / yellow thresholds with their return zones, the pattern choice
 * and the pattern times. Temperatures in tenths of °C, times in ms. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_ind.h"

#define BIT(a)  SH_ALARM_BIT(a)

static const sh_ind_params_t P = { .cold = 200, .hot = 900 };

/* Idle, collector 50 °C, Wi-Fi connected */
static sh_ind_in_t idle(void)
{
    sh_ind_in_t in;
    memset(&in, 0, sizeof(in));
    in.t_col = 500;
    in.wifi_connected = true;
    return in;
}

static sh_ind_out_t ind(const sh_ind_in_t *in)
{
    sh_ind_state_t st = { 0 };
    return sh_ind_update(&st, &P, in);
}

static sh_color_t color(const sh_ind_in_t *in)
{
    return ind(in).color;
}

static void test_colors(void)
{
    sh_ind_in_t in = idle();
    CHECK_EQ(color(&in), SH_COLOR_BLUE);
    in.pump_on = true;
    CHECK_EQ(color(&in), SH_COLOR_GREEN);

    in = idle();
    in.t_col = 100;
    CHECK_EQ(color(&in), SH_COLOR_WHITE);
    in.t_col = 950;
    CHECK_EQ(color(&in), SH_COLOR_YELLOW);
    in.vacation = true;
    CHECK_EQ(color(&in), SH_COLOR_PURPLE);

    /* before the first collector value / faulty collector sensor: no white, no yellow */
    in = idle();
    in.t_col = SH_TEMP_INVALID;
    CHECK_EQ(color(&in), SH_COLOR_BLUE);
    in.t_col = 100;
    in.col_fault = true;
    in.alarms = 0;                      /* (E1 alone would make it red) */
    CHECK_EQ(color(&in), SH_COLOR_BLUE);
}

static void test_priority(void)
{
    /* Each pair: the higher one wins. red > orange > purple > yellow > green > white > blue */
    sh_ind_in_t in = idle();
    in.t_col = 100;                                     /* white */
    CHECK_EQ(color(&in), SH_COLOR_WHITE);
    in.pump_on = true;                                  /* green over white */
    CHECK_EQ(color(&in), SH_COLOR_GREEN);

    in = idle();
    in.t_col = 950;
    in.pump_on = true;                                  /* yellow only with the pump stopped */
    CHECK_EQ(color(&in), SH_COLOR_GREEN);
    in.pump_on = false;
    CHECK_EQ(color(&in), SH_COLOR_YELLOW);
    in.vacation = true;                                 /* purple over yellow */
    CHECK_EQ(color(&in), SH_COLOR_PURPLE);
    in.pump_on = true;
    in.alarms = BIT(SH_ALARM_HOT);                      /* orange over purple, green, yellow */
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);
    in.vacation = false;
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);
    in.alarms = BIT(SH_ALARM_FRO);                      /* the frost protection: orange too */
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);
    in.t_col = 20;                                      /* over white (cold collector) */
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);
    in.vacation = true;
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);
    in.alarms = BIT(SH_ALARM_FRO) | BIT(SH_ALARM_E6);   /* red over it */
    CHECK_EQ(color(&in), SH_COLOR_RED);

    const sh_alarm_t alarms[] = { SH_ALARM_E1, SH_ALARM_E2, SH_ALARM_E4, SH_ALARM_E5, SH_ALARM_E6 };
    for (unsigned i = 0; i < sizeof(alarms) / sizeof(alarms[0]); i++) {
        in = idle();
        in.alarms = BIT(alarms[i]) | BIT(SH_ALARM_HOT);     /* red over orange */
        in.pump_on = true;
        in.vacation = true;
        CHECK_EQ(color(&in), SH_COLOR_RED);
        in.alarms = BIT(alarms[i]);
        in.pump_on = false;
        in.vacation = false;
        CHECK_EQ(color(&in), SH_COLOR_RED);
    }
}

static void test_thresholds(void)
{
    sh_ind_state_t st = { 0 };
    sh_ind_in_t in = idle();

    /* white: below 20 °C, released above 22 °C */
    in.t_col = 200;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_BLUE);
    in.t_col = 199;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_WHITE);
    in.t_col = 220;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_WHITE);
    in.t_col = 221;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_BLUE);
    in.t_col = 210;                                     /* in the zone, from above: stays blue */
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_BLUE);

    /* yellow: above 90 °C, released below 88 °C */
    in.t_col = 900;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_BLUE);
    in.t_col = 901;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_YELLOW);
    in.t_col = 880;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_YELLOW);
    in.t_col = 879;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_BLUE);
    in.t_col = 890;                                     /* in the zone, from below: stays blue */
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_BLUE);

    /* the memory survives a running pump: green, then yellow again when it stops */
    in.t_col = 950;
    sh_ind_update(&st, &P, &in);
    in.pump_on = true;
    in.t_col = 885;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_GREEN);
    in.pump_on = false;
    CHECK_EQ(sh_ind_update(&st, &P, &in).color, SH_COLOR_YELLOW);

    /* other thresholds from the parameters (Kconfig) */
    const sh_ind_params_t p2 = { .cold = 100, .hot = 800 };
    sh_ind_state_t st2 = { 0 };
    in = idle();
    in.t_col = 99;
    CHECK_EQ(sh_ind_update(&st2, &p2, &in).color, SH_COLOR_WHITE);
    in.t_col = 801;
    CHECK_EQ(sh_ind_update(&st2, &p2, &in).color, SH_COLOR_YELLOW);
}

static void test_patterns(void)
{
    sh_ind_in_t in = idle();
    CHECK_EQ(ind(&in).pattern, SH_IND_STEADY);
    in.wifi_connected = false;
    CHECK_EQ(ind(&in).pattern, SH_IND_HEARTBEAT);
    in.wifi_portal = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_HEARTBEAT);
    in.wifi_connected = true;                           /* portal open: not "online" */
    CHECK_EQ(ind(&in).pattern, SH_IND_HEARTBEAT);

    /* every non-red colour follows the connection */
    in = idle();
    in.alarms = BIT(SH_ALARM_HOT);
    in.pump_on = true;
    CHECK_EQ(ind(&in).pattern, SH_IND_STEADY);
    in.wifi_connected = false;
    CHECK_EQ(ind(&in).pattern, SH_IND_HEARTBEAT);
    CHECK_EQ(ind(&in).color, SH_COLOR_ORANGE);

    /* red: fast for E4 / E5, double for E1 / E2 / E6, fast for both groups; whatever the Wi-Fi */
    for (int wifi = 0; wifi < 2; wifi++) {
        in = idle();
        in.wifi_connected = wifi;
        in.alarms = BIT(SH_ALARM_E4);
        CHECK_EQ(ind(&in).pattern, SH_IND_FAST);
        in.alarms = BIT(SH_ALARM_E5);
        CHECK_EQ(ind(&in).pattern, SH_IND_FAST);
        in.alarms = BIT(SH_ALARM_E1);
        CHECK_EQ(ind(&in).pattern, SH_IND_DOUBLE);
        in.alarms = BIT(SH_ALARM_E2);
        CHECK_EQ(ind(&in).pattern, SH_IND_DOUBLE);
        in.alarms = BIT(SH_ALARM_E6);
        CHECK_EQ(ind(&in).pattern, SH_IND_DOUBLE);
        in.alarms = BIT(SH_ALARM_E6) | BIT(SH_ALARM_E4);
        CHECK_EQ(ind(&in).pattern, SH_IND_FAST);
        in.alarms = BIT(SH_ALARM_E1) | BIT(SH_ALARM_E2) | BIT(SH_ALARM_E5) | BIT(SH_ALARM_HOT);
        CHECK_EQ(ind(&in).pattern, SH_IND_FAST);
        CHECK_EQ(ind(&in).color, SH_COLOR_RED);
    }
}

static void test_pattern_times(void)
{
    /* steady */
    CHECK(sh_ind_lit(SH_IND_STEADY, 0));
    CHECK(sh_ind_lit(SH_IND_STEADY, 1234567));

    /* heartbeat: 80 ms every 2 s */
    CHECK(sh_ind_lit(SH_IND_HEARTBEAT, 0));
    CHECK(sh_ind_lit(SH_IND_HEARTBEAT, 79));
    CHECK(!sh_ind_lit(SH_IND_HEARTBEAT, 80));
    CHECK(!sh_ind_lit(SH_IND_HEARTBEAT, 1999));
    CHECK(sh_ind_lit(SH_IND_HEARTBEAT, 2000));
    CHECK(sh_ind_lit(SH_IND_HEARTBEAT, 2079));
    CHECK(!sh_ind_lit(SH_IND_HEARTBEAT, 2080));

    /* fast: 150 / 150 */
    CHECK(sh_ind_lit(SH_IND_FAST, 0));
    CHECK(sh_ind_lit(SH_IND_FAST, 149));
    CHECK(!sh_ind_lit(SH_IND_FAST, 150));
    CHECK(!sh_ind_lit(SH_IND_FAST, 299));
    CHECK(sh_ind_lit(SH_IND_FAST, 300));
    CHECK(!sh_ind_lit(SH_IND_FAST, 450));

    /* double: 100 on, 100 off, 100 on, 1000 dark (period 1300) */
    CHECK(sh_ind_lit(SH_IND_DOUBLE, 0));
    CHECK(sh_ind_lit(SH_IND_DOUBLE, 99));
    CHECK(!sh_ind_lit(SH_IND_DOUBLE, 100));
    CHECK(!sh_ind_lit(SH_IND_DOUBLE, 199));
    CHECK(sh_ind_lit(SH_IND_DOUBLE, 200));
    CHECK(sh_ind_lit(SH_IND_DOUBLE, 299));
    CHECK(!sh_ind_lit(SH_IND_DOUBLE, 300));
    CHECK(!sh_ind_lit(SH_IND_DOUBLE, 1299));
    CHECK(sh_ind_lit(SH_IND_DOUBLE, 1300));

    /* lit time per period, counted every ms */
    unsigned lit = 0;
    for (uint32_t ms = 0; ms < 1300; ms++) {
        lit += sh_ind_lit(SH_IND_DOUBLE, ms);
    }
    CHECK_EQ(lit, 200);
    lit = 0;
    for (uint32_t ms = 0; ms < 2000; ms++) {
        lit += sh_ind_lit(SH_IND_HEARTBEAT, ms);
    }
    CHECK_EQ(lit, 80);
}

/* Group C: the night cooling is orange (over the purple of the vacation), red over it */
static void test_cooling(void)
{
    sh_ind_in_t in = idle();
    in.pump_on = true;
    in.cooling = true;
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);
    in.vacation = true;                                 /* the vacation night */
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);
    in.cooling = false;                                 /* the session waits: purple */
    in.pump_on = false;
    CHECK_EQ(color(&in), SH_COLOR_PURPLE);
    in.cooling = true;
    in.pump_on = true;
    in.alarms = BIT(SH_ALARM_E4);
    CHECK_EQ(color(&in), SH_COLOR_RED);
    in.alarms = 0;
    in.wifi_connected = false;
    CHECK_EQ(ind(&in).pattern, SH_IND_HEARTBEAT);
}

int main(void)
{
    test_colors();
    test_priority();
    test_thresholds();
    test_patterns();
    test_pattern_times();
    test_cooling();
    return test_summary("test_ind");
}
