/* Night cooling: sessions, target, self-learning — see sh_cool.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_cool.h"

int16_t sh_cool_target(bool tubes, int16_t area, int16_t volume_l, int16_t tank_max, int8_t offset)
{
    /* Wh of a clear day; rise of the tank in tenths of K = Wh x 10 / (l x 1.163) */
    const int32_t wh = (int32_t)area * (tubes ? SH_COOL_TUBES_WH_M2 : SH_COOL_FLAT_WH_M2) / 10;
    const int32_t per_k = (int32_t)volume_l * 1163;             /* mWh / K */
    const int32_t rise = (wh * 10000 + per_k / 2) / per_k;
    const int32_t lo = tank_max - SH_COOL_RANGE;
    int32_t target = SH_COOL_TOP - rise;
    target = target < lo ? lo : target > tank_max ? tank_max : target;
    target += offset * 10;
    target = target < lo ? lo : target > tank_max ? tank_max : target;
    return (int16_t)target;
}

static void end_session(sh_cool_t *c, sh_cool_end_t why)
{
    if (c->trigger == SH_COOL_TMAX || c->trigger == SH_COOL_E4) {
        c->tonight = true;
    }
    c->trigger = SH_COOL_NONE;
    c->end = why;
    c->events |= SH_COOL_EVT_END;
}

/* The window of a) / b) closed: the night is over. */
static void morning(sh_cool_t *c, bool e4)
{
    c->e4_pending = false;
    c->cooled = c->tonight;             /* an E4 today tests the target of this night */
    c->tonight = false;

    const uint8_t quiet = c->quiet;
    const int8_t offset = c->offset;
    if (c->e4_seen) {
        c->quiet = 0;
    } else if (++c->quiet >= SH_COOL_QUIET_DAYS) {
        c->quiet = 0;
        if (c->offset < 0) {
            c->offset = (int8_t)(c->offset + SH_COOL_QUIET_STEP);
        }
    }
    c->e4_seen = e4;
    if (c->quiet != quiet || c->offset != offset) {
        c->events |= SH_COOL_EVT_LEARNED;
    }
}

void sh_cool_step(sh_cool_t *c, const sh_cool_in_t *in, uint32_t now_s)
{
    c->events = 0;
    const bool temps_ok = in->t_col != SH_TEMP_INVALID && in->t_tank != SH_TEMP_INVALID;

    /* E4: the trigger of the next night; on the day after a cooled night the target was too high */
    if (in->e4) {
        c->e4_pending = true;
        c->e4_seen = true;
        if (!c->e4_was && c->cooled) {
            c->cooled = false;
            const int offset = c->offset + SH_COOL_E4_STEP;
            c->offset = (int8_t)(offset < SH_COOL_OFFSET_MIN ? SH_COOL_OFFSET_MIN : offset);
            c->events |= SH_COOL_EVT_LEARNED;
        }
    }
    c->e4_was = in->e4;

    /* the windows of a) and b) */
    bool win_a, win_b;
    if (in->reliable) {
        win_b = in->min >= SH_COOL_B_FROM_MIN && in->min < SH_COOL_TO_MIN;
        win_a = in->min >= SH_COOL_A_FROM_MIN && in->min < SH_COOL_TO_MIN;
    } else {
        const uint32_t since = now_s - in->sunset_s;
        win_a = win_b = in->dark && since >= SH_COOL_AFTER_SUNSET_S &&
                        since < SH_COOL_AFTER_SUNSET_S + SH_COOL_NO_CLOCK_S;
    }
    /* a new night: the heater of the last one is forgotten */
    const bool night = win_b || (in->vacation && in->night);
    if (night && !c->was_night) {
        c->heater = false;
    }
    c->was_night = night;

    /* the heater: the tank rises although the pump cools it */
    if (!in->pump_cool || in->t_tank == SH_TEMP_INVALID) {
        c->run = false;
    } else if (!c->run) {
        c->run = true;
        c->run_min = in->t_tank;
    } else {
        if (in->t_tank < c->run_min) {
            c->run_min = in->t_tank;
        }
        if (in->t_tank - c->run_min >= SH_COOL_HEATER_RISE) {
            c->heater = true;
        }
    }

    /* the session that should be active now */
    sh_cool_trigger_t want = SH_COOL_NONE;
    sh_cool_end_t why = SH_COOL_END_TIME;
    if (!temps_ok) {
        why = SH_COOL_END_SENSOR;
    } else if (c->heater) {
        why = SH_COOL_END_HEATER;
    } else if (in->vacation) {
        why = c->trigger == SH_COOL_VACATION ? SH_COOL_END_TIME : SH_COOL_END_MODE;
        if (in->night) {
            want = SH_COOL_VACATION;
        }
    } else if (c->trigger == SH_COOL_VACATION) {
        why = SH_COOL_END_MODE;
    } else if (c->trigger != SH_COOL_NONE) {
        /* a running a) / b) session: until the target or the end of its window */
        if (in->t_tank <= in->target) {
            why = SH_COOL_END_TARGET;
        } else if (c->trigger == SH_COOL_E4 ? win_b : win_a) {
            want = c->trigger;
        }
    } else if (!c->tonight && in->t_tank > in->target) {
        if (win_b && c->e4_pending) {
            want = SH_COOL_E4;
        } else if (win_a && in->tmax_on && in->t_tank >= in->tank_max) {
            want = SH_COOL_TMAX;
        }
    }
    if (want != c->trigger) {
        if (c->trigger != SH_COOL_NONE) {
            end_session(c, why);
        }
        if (want != SH_COOL_NONE) {
            c->trigger = want;
            c->end = SH_COOL_END_NONE;
            c->events |= SH_COOL_EVT_START;
        }
    }
    c->target = in->target;

    /* pulse mode: a pulse every 30 min while a session runs or may start by the last values */
    c->pulse = c->trigger != SH_COOL_NONE ||
               (temps_ok && !c->heater && !in->vacation && !c->tonight && in->t_tank > in->target &&
                ((win_b && c->e4_pending) || (win_a && in->tmax_on && in->t_tank >= in->tank_max)));

    if (c->was_window && !win_b) {
        morning(c, in->e4);
    }
    c->was_window = win_b;
}
