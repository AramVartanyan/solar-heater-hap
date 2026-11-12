/* Plausibility of the sensor readings — see sh_meas.h. */

#include <stdlib.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_meas.h"

void sh_meas_init(sh_meas_t *m)
{
    m->last = SH_TEMP_INVALID;
    m->fails = 0;
    m->faulty = false;
}

static bool in_range(bool ok, int16_t t)
{
    return ok && t >= SH_MEAS_MIN && t <= SH_MEAS_MAX;
}

/* More than 20 K away from the last valid value (no jump without one). */
static bool jump(const sh_meas_t *m, int16_t t)
{
    return m->last != SH_TEMP_INVALID && abs((int)t - m->last) > SH_MEAS_JUMP;
}

bool sh_meas_accept(const sh_meas_t *m, bool ok, int16_t t)
{
    return in_range(ok, t) && !jump(m, t);
}

bool sh_meas_accept_retry(const sh_meas_t *m, bool ok1, int16_t t1, bool ok2, int16_t t2)
{
    if (!in_range(ok2, t2)) {
        return false;
    }
    if (!jump(m, t2)) {
        return true;
    }
    /* a jump twice in a row, the same value: a real fast change (e.g. the sensor in boiling water) */
    return in_range(ok1, t1) && jump(m, t1) && abs((int)t2 - t1) <= SH_MEAS_CONFIRM;
}

bool sh_meas_update(sh_meas_t *m, bool success, int16_t t)
{
    const bool was_faulty = m->faulty;
    if (success) {
        m->last = t;
        m->fails = 0;
        m->faulty = false;
    } else {
        if (m->fails < SH_MEAS_FAULT_COUNT) {
            m->fails++;
        }
        m->faulty = m->fails >= SH_MEAS_FAULT_COUNT;
    }
    return m->faulty != was_faulty;
}
