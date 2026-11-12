/* Host test of the judging of the sensor readings (main/sh_sensor_logic.c) — no ESP-IDF needed.
 *
 *   make -C project/solar-heater-hap/test
 *
 * doc/operating-modes.md 2.1 (item 12); doc/tasks/step2-settings-menu-pump-sensors.md 3.5.
 * Temperatures are tenths of °C. */

#include "sh_state.h"
#include "sh_sensor_logic.h"
#include "test_util.h"

/* A sensor whose last valid value is `t`. */
static sh_sensor_t sensor_at(int16_t t)
{
    sh_sensor_t s;
    sh_sensor_init(&s);
    CHECK(sh_sensor_first(&s, true, t));
    return s;
}

/* One measurement that fails: no answer, twice. */
static void fail(sh_sensor_t *s)
{
    CHECK(!sh_sensor_first(s, false, 0));
    CHECK(!sh_sensor_second(s, false, 0, false, 0));
}

static void test_initial_state(void)
{
    sh_sensor_t s;
    sh_sensor_init(&s);
    CHECK(s.value == SH_TEMP_INVALID && s.fails == 0 && !s.fault);
}

static void test_good_reading(void)
{
    sh_sensor_t s;
    sh_sensor_init(&s);
    CHECK(sh_sensor_first(&s, true, 253));
    CHECK(s.value == 253 && s.fails == 0 && !s.fault);
    CHECK(sh_sensor_first(&s, true, 251));
    CHECK(s.value == 251);

    sh_sensor_init(&s);
    CHECK(sh_sensor_first(&s, true, -105));     /* the first value: nothing to compare it with */
    CHECK(s.value == -105);
}

static void test_no_answer(void)
{
    sh_sensor_t s = sensor_at(253);
    CHECK(!sh_sensor_first(&s, false, 0));      /* a second reading is needed */
    CHECK(s.value == 253 && s.fails == 0);      /* nothing counted yet */
    CHECK(sh_sensor_second(&s, false, 0, true, 255));   /* the second one is good */
    CHECK(s.value == 255 && s.fails == 0 && !s.fault);

    CHECK(!sh_sensor_first(&s, false, 0));
    CHECK(!sh_sensor_second(&s, false, 0, false, 0));   /* both failed: the measurement failed */
    CHECK(s.value == 255 && s.fails == 1 && !s.fault);  /* the last valid value stays */
}

static void test_out_of_range(void)
{
    sh_sensor_t s;
    sh_sensor_init(&s);
    CHECK(!sh_sensor_first(&s, true, -551));    /* below -55.0 °C */
    CHECK(sh_sensor_first(&s, true, -550));
    CHECK(s.value == -550);

    sh_sensor_init(&s);
    CHECK(!sh_sensor_first(&s, true, 1251));    /* above +125.0 °C */
    CHECK(sh_sensor_first(&s, true, 1250));
    CHECK(s.value == 1250);

    s = sensor_at(1200);
    CHECK(!sh_sensor_first(&s, true, 1251));
    CHECK(!sh_sensor_second(&s, true, 1251, true, 1251));   /* the same again: still impossible */
    CHECK(s.value == 1200 && s.fails == 1);
    CHECK(!sh_sensor_first(&s, true, 1300));
    CHECK(sh_sensor_second(&s, true, 1300, true, 1210));
    CHECK(s.value == 1210 && s.fails == 0);
}

static void test_jump_threshold(void)
{
    sh_sensor_t s = sensor_at(300);
    CHECK(sh_sensor_first(&s, true, 500));      /* exactly + 20.0 K: not a jump */
    s = sensor_at(300);
    CHECK(!sh_sensor_first(&s, true, 501));     /* + 20.1 K */
    CHECK(s.value == 300);
    s = sensor_at(300);
    CHECK(sh_sensor_first(&s, true, 100));      /* exactly - 20.0 K */
    s = sensor_at(300);
    CHECK(!sh_sensor_first(&s, true, 99));      /* - 20.1 K */
}

static void test_jump_confirmed(void)
{
    sh_sensor_t s = sensor_at(300);             /* e.g. the sensor put into boiling water */
    CHECK(!sh_sensor_first(&s, true, 900));
    CHECK(sh_sensor_second(&s, true, 900, true, 910));  /* + 1.0 K from the jump: real */
    CHECK(s.value == 910 && s.fails == 0);

    s = sensor_at(300);
    CHECK(!sh_sensor_first(&s, true, 900));
    CHECK(sh_sensor_second(&s, true, 900, true, 890));  /* - 1.0 K */
    CHECK(s.value == 890);
}

static void test_jump_not_confirmed(void)
{
    sh_sensor_t s = sensor_at(300);
    CHECK(!sh_sensor_first(&s, true, 900));
    CHECK(!sh_sensor_second(&s, true, 900, true, 911)); /* + 1.1 K from the jump */
    CHECK(s.value == 300 && s.fails == 1);

    s = sensor_at(300);
    CHECK(!sh_sensor_first(&s, true, 900));
    CHECK(!sh_sensor_second(&s, true, 900, true, 889)); /* - 1.1 K */
    CHECK(s.value == 300 && s.fails == 1);

    s = sensor_at(300);
    CHECK(!sh_sensor_first(&s, true, 900));
    CHECK(!sh_sensor_second(&s, true, 900, false, 0));  /* no answer */
    CHECK(s.value == 300 && s.fails == 1);

    s = sensor_at(300);                         /* back near the old value: the measurement failed */
    CHECK(!sh_sensor_first(&s, true, 900));     /* (the next one, 10 s later, is good again) */
    CHECK(!sh_sensor_second(&s, true, 900, true, 301));
    CHECK(s.value == 300 && s.fails == 1);
    CHECK(sh_sensor_first(&s, true, 301));
    CHECK(s.value == 301 && s.fails == 0);

    s = sensor_at(1240);                        /* a jump confirmed by an impossible value is not one */
    CHECK(!sh_sensor_first(&s, true, 1245 - 300));
    CHECK(!sh_sensor_second(&s, true, 945, true, 1260));
    CHECK(s.value == 1240 && s.fails == 1);
}

static void test_jump_after_a_failed_reading(void)
{
    sh_sensor_t s = sensor_at(300);
    CHECK(!sh_sensor_first(&s, false, 0));
    CHECK(!sh_sensor_second(&s, false, 0, true, 900));  /* a jump with nothing to confirm it */
    CHECK(s.value == 300 && s.fails == 1);
}

static void test_fault_after_5_measurements(void)
{
    sh_sensor_t s = sensor_at(300);
    for (int i = 1; i <= 4; i++) {
        fail(&s);
        CHECK(s.fails == i && !s.fault && s.value == 300);
    }
    fail(&s);                                   /* the 5th in a row */
    CHECK(s.fault && s.value == 300);
    fail(&s);
    CHECK(s.fault && s.fails == SH_SENSOR_FAULT_COUNT);     /* the count does not run away */
}

static void test_good_measurement_clears_the_count(void)
{
    sh_sensor_t s = sensor_at(300);
    for (int i = 0; i < 4; i++) {
        fail(&s);
    }
    CHECK(sh_sensor_first(&s, true, 310));      /* 4 failed, 1 good: the count starts again */
    CHECK(s.fails == 0);
    for (int i = 0; i < 4; i++) {
        fail(&s);
    }
    CHECK(!s.fault);
    fail(&s);
    CHECK(s.fault);
}

static void test_recovery(void)
{
    sh_sensor_t s = sensor_at(300);
    for (int i = 0; i < 5; i++) {
        fail(&s);
    }
    CHECK(s.fault);
    CHECK(sh_sensor_first(&s, true, 320));      /* one good measurement */
    CHECK(!s.fault && s.fails == 0 && s.value == 320);

    s = sensor_at(300);                         /* after a long fault the temperature is elsewhere: */
    for (int i = 0; i < 5; i++) {               /* the jump is confirmed by the second reading */
        fail(&s);
    }
    CHECK(!sh_sensor_first(&s, true, 700));
    CHECK(sh_sensor_second(&s, true, 700, true, 701));
    CHECK(!s.fault && s.fails == 0 && s.value == 701);
}

static void test_never_answered(void)
{
    sh_sensor_t s;
    sh_sensor_init(&s);
    for (int i = 0; i < 4; i++) {
        fail(&s);
        CHECK(!s.fault && s.value == SH_TEMP_INVALID);
    }
    fail(&s);
    CHECK(s.fault && s.value == SH_TEMP_INVALID);
    CHECK(sh_sensor_first(&s, true, 900));      /* no last value: nothing to jump from */
    CHECK(!s.fault && s.value == 900);
}

int main(void)
{
    test_initial_state();
    test_good_reading();
    test_no_answer();
    test_out_of_range();
    test_jump_threshold();
    test_jump_confirmed();
    test_jump_not_confirmed();
    test_jump_after_a_failed_reading();
    test_fault_after_5_measurements();
    test_good_measurement_clears_the_count();
    test_recovery();
    test_never_answered();
    return test_result("sensor logic");
}
