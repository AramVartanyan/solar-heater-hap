/* Host test of the status LED logic (main/sh_led_logic.c): state -> (colour, pattern) and the times
 * of the patterns (doc/led-indication.md; doc/tasks/step5-alarms-indication.md 3.3).
 * Temperatures are tenths of °C, times are milliseconds. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarms.h"
#include "sh_led_logic.h"

/* Defaults of the task (Kconfig "Device Settings") */
static const sh_led_params_t L = { .cold = 200, .hot = 900 };

#define INV     SH_TEMP_INVALID
#define E1      SH_ALARM_BIT(SH_ALARM_E1)
#define E2      SH_ALARM_BIT(SH_ALARM_E2)
#define E4      SH_ALARM_BIT(SH_ALARM_E4)
#define E5      SH_ALARM_BIT(SH_ALARM_E5)
#define E6      SH_ALARM_BIT(SH_ALARM_E6)
#define HOT     SH_ALARM_BIT(SH_ALARM_HOT)

static sh_led_mem_t s_mem;

/* Idle, online, collector at 50 °C. */
static sh_led_in_t idle(void)
{
    sh_led_in_t in;
    memset(&in, 0, sizeof(in));
    in.t_col = 500;
    in.wifi_connected = true;
    return in;
}

static sh_led_out_t decide(const sh_led_in_t *in)
{
    sh_led_out_t out;
    sh_led_decide(&L, &s_mem, in, &out);
    return out;
}

/* Colour with a fresh memory of the return zones. */
static sh_color_t color(const sh_led_in_t *in)
{
    memset(&s_mem, 0, sizeof(s_mem));
    return decide(in).color;
}

static sh_led_pattern_t pattern(const sh_led_in_t *in)
{
    memset(&s_mem, 0, sizeof(s_mem));
    return decide(in).pattern;
}

/* Colour at this collector temperature, keeping the memory. */
static sh_color_t at(sh_led_in_t *in, int16_t t_col)
{
    in->t_col = t_col;
    return decide(in).color;
}

static void test_colors(void)
{
    sh_led_in_t in = idle();
    CHECK_EQ(color(&in), SH_COLOR_BLUE);        /* idle, collector 20 ... 90 °C */

    in.t_col = 100;
    CHECK_EQ(color(&in), SH_COLOR_WHITE);       /* cold collector */

    in.pump_on = true;
    CHECK_EQ(color(&in), SH_COLOR_GREEN);       /* the pump runs: green over white */
    in.t_col = 500;
    CHECK_EQ(color(&in), SH_COLOR_GREEN);

    in.t_col = 950;
    CHECK_EQ(color(&in), SH_COLOR_GREEN);       /* hot collector, pump running: not yellow */
    in.pump_on = false;
    CHECK_EQ(color(&in), SH_COLOR_YELLOW);      /* hot collector, pump stopped */

    in.vacation = true;
    CHECK_EQ(color(&in), SH_COLOR_PURPLE);      /* vacation over yellow ... */
    in.pump_on = true;
    CHECK_EQ(color(&in), SH_COLOR_PURPLE);      /* ... over green ... */
    in.pump_on = false;
    in.t_col = 100;
    CHECK_EQ(color(&in), SH_COLOR_PURPLE);      /* ... over white ... */
    in.t_col = 500;
    CHECK_EQ(color(&in), SH_COLOR_PURPLE);      /* ... and over blue */

    in.alarms = HOT;
    in.pump_on = true;
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);      /* protection over vacation */
    in.vacation = false;
    in.t_col = 1100;
    CHECK_EQ(color(&in), SH_COLOR_ORANGE);      /* over yellow (collector above 90 °C) and green */

    /* every alarm is red, over everything */
    static const uint8_t alarms[] = { E1, E2, E4, E5, E6 };
    for (unsigned i = 0; i < sizeof(alarms); i++) {
        in = idle();
        in.alarms = alarms[i];
        CHECK_EQ(color(&in), SH_COLOR_RED);
        in.alarms |= HOT;
        in.pump_on = true;
        in.vacation = true;
        in.t_col = 1100;
        CHECK_EQ(color(&in), SH_COLOR_RED);
        in.t_col = 100;
        CHECK_EQ(color(&in), SH_COLOR_RED);
    }

    /* red is only for the alarms */
    in = idle();
    in.alarms = HOT;
    CHECK(color(&in) != SH_COLOR_RED);
}

static void test_thresholds(void)
{
    /* white: below 20 °C, until above 22 °C */
    sh_led_in_t in = idle();
    memset(&s_mem, 0, sizeof(s_mem));
    CHECK_EQ(at(&in, 201), SH_COLOR_BLUE);
    CHECK_EQ(at(&in, 200), SH_COLOR_BLUE);      /* 20.0 °C: not below */
    CHECK_EQ(at(&in, 199), SH_COLOR_WHITE);     /* 19.9 °C */
    CHECK_EQ(at(&in, 210), SH_COLOR_WHITE);
    CHECK_EQ(at(&in, 220), SH_COLOR_WHITE);     /* 22.0 °C: not above */
    CHECK_EQ(at(&in, 221), SH_COLOR_BLUE);      /* 22.1 °C */
    CHECK_EQ(at(&in, 210), SH_COLOR_BLUE);      /* in the return zone from above: blue */
    CHECK_EQ(at(&in, -50), SH_COLOR_WHITE);

    /* yellow: above 90 °C, until below 88 °C */
    memset(&s_mem, 0, sizeof(s_mem));
    CHECK_EQ(at(&in, 899), SH_COLOR_BLUE);
    CHECK_EQ(at(&in, 900), SH_COLOR_BLUE);      /* 90.0 °C: not above */
    CHECK_EQ(at(&in, 901), SH_COLOR_YELLOW);    /* 90.1 °C */
    CHECK_EQ(at(&in, 890), SH_COLOR_YELLOW);
    CHECK_EQ(at(&in, 880), SH_COLOR_YELLOW);    /* 88.0 °C: not below */
    CHECK_EQ(at(&in, 879), SH_COLOR_BLUE);      /* 87.9 °C */
    CHECK_EQ(at(&in, 890), SH_COLOR_BLUE);      /* in the return zone from below: blue */

    /* the zones are followed also while another colour is shown */
    memset(&s_mem, 0, sizeof(s_mem));
    in.pump_on = true;
    CHECK_EQ(at(&in, 950), SH_COLOR_GREEN);
    CHECK_EQ(at(&in, 890), SH_COLOR_GREEN);
    in.pump_on = false;
    CHECK_EQ(at(&in, 890), SH_COLOR_YELLOW);    /* the pump stopped inside the return zone */

    /* no collector value (none yet, or the sensor is faulty): the zones stay as they are */
    memset(&s_mem, 0, sizeof(s_mem));
    CHECK_EQ(at(&in, INV), SH_COLOR_BLUE);
    CHECK_EQ(at(&in, 100), SH_COLOR_WHITE);
    CHECK_EQ(at(&in, INV), SH_COLOR_WHITE);
    CHECK_EQ(at(&in, 950), SH_COLOR_YELLOW);
    CHECK_EQ(at(&in, INV), SH_COLOR_YELLOW);

    /* other thresholds */
    const sh_led_params_t p = { .cold = 50, .hot = 1000 };
    sh_led_mem_t m = { 0 };
    sh_led_out_t out;
    in = idle();
    in.t_col = 50;
    sh_led_decide(&p, &m, &in, &out);
    CHECK_EQ(out.color, SH_COLOR_BLUE);
    in.t_col = 49;
    sh_led_decide(&p, &m, &in, &out);
    CHECK_EQ(out.color, SH_COLOR_WHITE);
    in.t_col = 1001;
    sh_led_decide(&p, &m, &in, &out);
    CHECK_EQ(out.color, SH_COLOR_YELLOW);
    in.t_col = 979;
    sh_led_decide(&p, &m, &in, &out);
    CHECK_EQ(out.color, SH_COLOR_BLUE);
}

static void test_patterns(void)
{
    /* the connection: steady with Wi-Fi, a flash without it or with the setup portal open */
    sh_led_in_t in = idle();
    CHECK_EQ(pattern(&in), SH_LED_STEADY);
    in.wifi_connected = false;
    CHECK_EQ(pattern(&in), SH_LED_FLASH);
    in.wifi_portal = true;
    CHECK_EQ(pattern(&in), SH_LED_FLASH);
    in.wifi_connected = true;
    CHECK_EQ(pattern(&in), SH_LED_FLASH);

    /* the same for every colour but red */
    for (int online = 0; online <= 1; online++) {
        const sh_led_pattern_t want = online ? SH_LED_STEADY : SH_LED_FLASH;
        in = idle();
        in.wifi_connected = online;
        in.t_col = 100;
        CHECK_EQ(color(&in), SH_COLOR_WHITE);
        CHECK_EQ(pattern(&in), want);
        in.t_col = 950;
        CHECK_EQ(color(&in), SH_COLOR_YELLOW);
        CHECK_EQ(pattern(&in), want);
        in.pump_on = true;
        CHECK_EQ(color(&in), SH_COLOR_GREEN);
        CHECK_EQ(pattern(&in), want);
        in.alarms = HOT;
        CHECK_EQ(color(&in), SH_COLOR_ORANGE);
        CHECK_EQ(pattern(&in), want);
        in.alarms = 0;
        in.vacation = true;
        CHECK_EQ(color(&in), SH_COLOR_PURPLE);
        CHECK_EQ(pattern(&in), want);
    }

    /* red only blinks, whatever the connection */
    for (int wifi = 0; wifi < 4; wifi++) {
        in = idle();
        in.wifi_connected = wifi & 1;
        in.wifi_portal = (wifi & 2) != 0;
        in.alarms = E4;
        CHECK_EQ(pattern(&in), SH_LED_FAST);    /* overheating: fast */
        in.alarms = E5;
        CHECK_EQ(pattern(&in), SH_LED_FAST);
        in.alarms = E1;
        CHECK_EQ(pattern(&in), SH_LED_DOUBLE);  /* failure: double */
        in.alarms = E2;
        CHECK_EQ(pattern(&in), SH_LED_DOUBLE);
        in.alarms = E6;
        CHECK_EQ(pattern(&in), SH_LED_DOUBLE);
        in.alarms = E1 | E2 | E6 | HOT;
        CHECK_EQ(pattern(&in), SH_LED_DOUBLE);
        in.alarms = E6 | E4;                    /* both groups: fast */
        CHECK_EQ(pattern(&in), SH_LED_FAST);
        in.alarms = E1 | E2 | E5 | E6;
        CHECK_EQ(pattern(&in), SH_LED_FAST);
    }
}

/* Lit `ms` after the start of the pattern? */
static bool lit(sh_led_pattern_t p, uint32_t ms)
{
    uint32_t next;
    return sh_led_pattern_lit(p, ms, &next);
}

static uint32_t next(sh_led_pattern_t p, uint32_t ms)
{
    uint32_t n = 12345;
    sh_led_pattern_lit(p, ms, &n);
    return n;
}

static void test_pattern_times(void)
{
    /* steady: always lit, no change */
    CHECK_EQ(sh_led_pattern_period(SH_LED_STEADY), 0);
    CHECK(lit(SH_LED_STEADY, 0));
    CHECK(lit(SH_LED_STEADY, 123456));
    CHECK_EQ(next(SH_LED_STEADY, 0), 0);

    /* flash: 80 ms every 2 s */
    CHECK_EQ(sh_led_pattern_period(SH_LED_FLASH), 2000);
    CHECK(lit(SH_LED_FLASH, 0));
    CHECK(lit(SH_LED_FLASH, 79));
    CHECK(!lit(SH_LED_FLASH, 80));
    CHECK(!lit(SH_LED_FLASH, 1999));
    CHECK(lit(SH_LED_FLASH, 2000));
    CHECK_EQ(next(SH_LED_FLASH, 0), 80);
    CHECK_EQ(next(SH_LED_FLASH, 79), 1);
    CHECK_EQ(next(SH_LED_FLASH, 80), 1920);
    CHECK_EQ(next(SH_LED_FLASH, 1999), 1);

    /* fast: 150 ms on, 150 ms off */
    CHECK_EQ(sh_led_pattern_period(SH_LED_FAST), 300);
    CHECK(lit(SH_LED_FAST, 0));
    CHECK(lit(SH_LED_FAST, 149));
    CHECK(!lit(SH_LED_FAST, 150));
    CHECK(!lit(SH_LED_FAST, 299));
    CHECK(lit(SH_LED_FAST, 300));
    CHECK_EQ(next(SH_LED_FAST, 0), 150);
    CHECK_EQ(next(SH_LED_FAST, 149), 1);
    CHECK_EQ(next(SH_LED_FAST, 150), 150);
    CHECK_EQ(next(SH_LED_FAST, 299), 1);

    /* double: 2 x 100 ms, 100 ms apart, then 1 s dark */
    CHECK_EQ(sh_led_pattern_period(SH_LED_DOUBLE), 1300);
    CHECK(lit(SH_LED_DOUBLE, 0));
    CHECK(lit(SH_LED_DOUBLE, 99));
    CHECK(!lit(SH_LED_DOUBLE, 100));
    CHECK(!lit(SH_LED_DOUBLE, 199));
    CHECK(lit(SH_LED_DOUBLE, 200));
    CHECK(lit(SH_LED_DOUBLE, 299));
    CHECK(!lit(SH_LED_DOUBLE, 300));
    CHECK(!lit(SH_LED_DOUBLE, 1299));
    CHECK(lit(SH_LED_DOUBLE, 1300));
    CHECK_EQ(next(SH_LED_DOUBLE, 0), 100);
    CHECK_EQ(next(SH_LED_DOUBLE, 100), 100);
    CHECK_EQ(next(SH_LED_DOUBLE, 200), 100);
    CHECK_EQ(next(SH_LED_DOUBLE, 300), 1000);
    CHECK_EQ(next(SH_LED_DOUBLE, 1299), 1);

    /* Driven as sh_led does it (a timer for every next change): the light changes at the times
     * above, period after period, and the lit time of a period is right. */
    static const struct {
        sh_led_pattern_t pattern;
        uint32_t lit_ms;
        int changes;
    } drive[] = {
        { SH_LED_FLASH, 80, 2 }, { SH_LED_FAST, 150, 2 }, { SH_LED_DOUBLE, 200, 4 },
    };
    for (unsigned i = 0; i < sizeof(drive) / sizeof(drive[0]); i++) {
        const uint32_t period = sh_led_pattern_period(drive[i].pattern);
        uint32_t ms = 0, lit_ms = 0;
        int changes = 0;
        while (ms < 3 * period) {
            uint32_t step;
            const bool on = sh_led_pattern_lit(drive[i].pattern, ms % period, &step);
            CHECK(step > 0);
            if (step == 0) {
                break;
            }
            CHECK_EQ(on, lit(drive[i].pattern, ms + step - 1));     /* no change in between */
            CHECK(on != lit(drive[i].pattern, ms + step));          /* a change at the end */
            lit_ms += on ? step : 0;
            ms += step;
            changes++;
        }
        CHECK_EQ(ms, 3 * period);
        CHECK_EQ(lit_ms, 3 * drive[i].lit_ms);
        CHECK_EQ(changes, 3 * drive[i].changes);
    }
}

int main(void)
{
    test_colors();
    test_thresholds();
    test_patterns();
    test_pattern_times();
    return test_summary("test_led_logic");
}
