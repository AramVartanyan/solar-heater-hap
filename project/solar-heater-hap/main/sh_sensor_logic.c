/* Judging the readings of one temperature sensor — see sh_sensor_logic.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_sensor_logic.h"

typedef enum { READ_GOOD, READ_FAILED, READ_JUMP } read_t;

static read_t judge(const sh_sensor_t *s, bool ok, int16_t t)
{
    if (!ok || t < SH_SENSOR_T_MIN || t > SH_SENSOR_T_MAX) {
        return READ_FAILED;
    }
    if (s->value != SH_TEMP_INVALID) {
        const int32_t d = (int32_t)t - s->value;
        if (d > SH_SENSOR_JUMP || d < -SH_SENSOR_JUMP) {
            return READ_JUMP;
        }
    }
    return READ_GOOD;
}

static void accept(sh_sensor_t *s, int16_t t)
{
    s->value = t;
    s->fails = 0;
    s->fault = false;
}

void sh_sensor_init(sh_sensor_t *s)
{
    s->value = SH_TEMP_INVALID;
    s->fails = 0;
    s->fault = false;
}

bool sh_sensor_first(sh_sensor_t *s, bool ok, int16_t t)
{
    if (judge(s, ok, t) != READ_GOOD) {
        return false;
    }
    accept(s, t);
    return true;
}

bool sh_sensor_second(sh_sensor_t *s, bool ok1, int16_t t1, bool ok2, int16_t t2)
{
    const read_t second = judge(s, ok2, t2);
    bool good;
    if (judge(s, ok1, t1) == READ_JUMP) {
        /* a real fast change shows the same value again; anything else was not a temperature */
        const int32_t d = (int32_t)t2 - t1;
        good = second != READ_FAILED && d <= SH_SENSOR_JUMP_CONFIRM && d >= -SH_SENSOR_JUMP_CONFIRM;
    } else {
        good = second == READ_GOOD;
    }
    if (good) {
        accept(s, t2);
        return true;
    }
    if (s->fails < SH_SENSOR_FAULT_COUNT) {
        s->fails++;
    }
    if (s->fails >= SH_SENSOR_FAULT_COUNT) {
        s->fault = true;
    }
    return false;
}
