/* Host test of the sensor reading checks and the sensor fault (main/sh_meas.c;
 * doc/tasks/step2-settings-menu-pump-sensors.md 3.5). Temperatures in tenths of °C. */

#include "test_util.h"
#include "sh_state.h"
#include "sh_meas.h"

static sh_meas_t with_last(int16_t last)
{
    sh_meas_t m;
    sh_meas_init(&m);
    if (last != SH_TEMP_INVALID) {
        sh_meas_update(&m, true, last);
    }
    return m;
}

static void test_init(void)
{
    sh_meas_t m;
    sh_meas_init(&m);
    CHECK_EQ(m.last, SH_TEMP_INVALID);
    CHECK_EQ(m.fails, 0);
    CHECK(!m.faulty);
}

static void test_no_answer(void)
{
    sh_meas_t m = with_last(400);
    CHECK(!sh_meas_accept(&m, false, 400));         /* no answer or CRC */
    CHECK(sh_meas_accept(&m, true, 400));
}

static void test_range(void)
{
    sh_meas_t m = with_last(SH_TEMP_INVALID);       /* no last value: no jump check */
    CHECK(sh_meas_accept(&m, true, -550));
    CHECK(!sh_meas_accept(&m, true, -551));
    CHECK(sh_meas_accept(&m, true, 1250));
    CHECK(!sh_meas_accept(&m, true, 1251));
    CHECK(sh_meas_accept(&m, true, 850));
}

static void test_jump(void)
{
    sh_meas_t m = with_last(400);
    CHECK(sh_meas_accept(&m, true, 600));           /* exactly 20.0 K */
    CHECK(!sh_meas_accept(&m, true, 601));          /* 20.1 K */
    CHECK(sh_meas_accept(&m, true, 200));
    CHECK(!sh_meas_accept(&m, true, 199));
}

static void test_retry_after_jump(void)
{
    sh_meas_t m = with_last(400);
    CHECK(sh_meas_accept_retry(&m, true, 700, true, 710));      /* within 1.0 K of the jump */
    CHECK(sh_meas_accept_retry(&m, true, 700, true, 690));
    CHECK(!sh_meas_accept_retry(&m, true, 700, true, 711));     /* 1.1 K */
    CHECK(!sh_meas_accept_retry(&m, true, 700, true, 689));
    CHECK(sh_meas_accept_retry(&m, true, 700, true, 405));      /* back to normal */
    CHECK(!sh_meas_accept_retry(&m, true, 700, false, 700));    /* no answer */
    CHECK(!sh_meas_accept_retry(&m, true, 1240, true, 1251));   /* out of range */
}

static void test_retry_after_failure(void)
{
    sh_meas_t m = with_last(400);
    CHECK(sh_meas_accept_retry(&m, false, 0, true, 401));       /* no answer, then OK */
    CHECK(!sh_meas_accept_retry(&m, false, 0, true, 700));      /* then a jump: nothing to confirm it */
    CHECK(!sh_meas_accept_retry(&m, true, 1300, true, 700));    /* out of range, then a jump */
    CHECK(sh_meas_accept_retry(&m, true, 1300, true, 410));
    CHECK(!sh_meas_accept_retry(&m, false, 0, false, 0));

    m = with_last(SH_TEMP_INVALID);
    CHECK(sh_meas_accept_retry(&m, false, 0, true, 900));       /* first value ever */
}

static void test_fault_after_5(void)
{
    sh_meas_t m = with_last(400);
    for (int i = 1; i <= 4; i++) {
        CHECK(!sh_meas_update(&m, false, 0));
        CHECK_EQ(m.fails, i);
        CHECK(!m.faulty);
        CHECK_EQ(m.last, 400);                      /* the last valid value is kept */
    }
    CHECK(sh_meas_update(&m, false, 0));            /* 5th: faulty, changed */
    CHECK(m.faulty);
    CHECK_EQ(m.last, 400);
    CHECK(!sh_meas_update(&m, false, 0));           /* stays faulty */
    CHECK(m.faulty);
    CHECK_EQ(m.fails, 5);

    CHECK(sh_meas_update(&m, true, 420));           /* a good measurement clears it */
    CHECK(!m.faulty);
    CHECK_EQ(m.fails, 0);
    CHECK_EQ(m.last, 420);
}

static void test_fails_must_be_in_a_row(void)
{
    sh_meas_t m = with_last(400);
    for (int i = 0; i < 4; i++) {
        sh_meas_update(&m, false, 0);
    }
    sh_meas_update(&m, true, 401);
    for (int i = 0; i < 4; i++) {
        sh_meas_update(&m, false, 0);
    }
    CHECK(!m.faulty);
}

static void test_jump_compares_with_last_valid(void)
{
    sh_meas_t m = with_last(400);
    sh_meas_update(&m, false, 0);                   /* failed measurements do not move it */
    CHECK(!sh_meas_accept(&m, true, 601));
    sh_meas_update(&m, true, 600);
    CHECK(sh_meas_accept(&m, true, 800));
}

int main(void)
{
    test_init();
    test_no_answer();
    test_range();
    test_jump();
    test_retry_after_jump();
    test_retry_after_failure();
    test_fault_after_5();
    test_fails_must_be_in_a_row();
    test_jump_compares_with_last_valid();
    return test_summary("test_meas");
}
