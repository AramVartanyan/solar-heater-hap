/* Night cooling and the vacation — see sh_cool.h. */

#include <stddef.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_sun.h"             /* SH_SUN_NONE */
#include "sh_cool.h"

/* The local time passed `mark` since the previous step. */
static bool crossed(int16_t last, uint16_t min, int mark)
{
    if (last < 0) {
        return false;
    }
    return last < mark ? (min >= mark || min < last) : (min >= mark && min < last);
}

static bool in_window(uint16_t min, int from)
{
    return min >= from && min < SH_COOL_UNTIL;
}

bool sh_cool_vacation_night(const sh_cool_in_t *in)
{
    if (in->clock && in->avg_rise != SH_SUN_NONE && in->avg_set != SH_SUN_NONE) {
        return in->min >= in->avg_set || in->min < in->avg_rise;
    }
    return in->night;
}

/* A heater: the tank rose >= 0.5 K within the last 10 min of the session. */
static bool heater(sh_cool_t *c, int16_t tank, uint32_t now_s)
{
    if (now_s < c->next_s) {
        return false;
    }
    c->tank[c->head] = tank;
    c->head = (uint8_t)((c->head + 1) % SH_COOL_SAMPLES);
    if (c->n < SH_COOL_SAMPLES) {
        c->n++;
    }
    c->next_s = now_s + 60;
    if (c->n < SH_COOL_SAMPLES) {
        return false;
    }
    const int16_t oldest = c->tank[c->head];
    const int16_t newest = c->tank[(c->head + SH_COOL_SAMPLES - 1) % SH_COOL_SAMPLES];
    return newest - oldest >= SH_COOL_HEATER_RISE;
}

static void end(sh_cool_t *c, sh_cool_end_t why)
{
    c->ended = c->why;
    c->end = why;
    c->events |= SH_COOL_EVT_END;
    if (c->why != SH_COOL_VACATION && why != SH_COOL_END_SENSOR && why != SH_COOL_END_MODE) {
        c->done = true;
    }
    if (why == SH_COOL_END_HEATER) {
        c->heater = true;
    }
    c->why = SH_COOL_NONE;
    c->target = SH_TEMP_INVALID;
}

static void start(sh_cool_t *c, const sh_cool_in_t *in, sh_cool_why_t why, uint32_t now_s)
{
    c->why = why;
    c->events |= SH_COOL_EVT_START;
    c->n = 0;
    c->head = 0;
    c->next_s = now_s;
    c->until = SH_SUN_NONE;
    if (why == SH_COOL_VACATION) {
        c->target = SH_TEMP_INVALID;
        if (in->clock && in->avg_rise != SH_SUN_NONE && in->avg_set != SH_SUN_NONE) {
            c->until = in->avg_rise;
        }
    } else {
        c->target = in->target;
        c->watch = true;
        if (in->clock) {
            c->until = SH_COOL_UNTIL;
            if (in->avg_rise != SH_SUN_NONE && in->avg_rise > in->min && in->avg_rise < SH_COOL_UNTIL) {
                c->until = in->avg_rise;
            }
        }
    }
}

void sh_cool_step(sh_cool_t *c, sh_cool_learn_t *learn, const sh_cool_in_t *in, uint32_t now_s)
{
    c->events = 0;
    const int16_t last = c->last_min;
    c->last_min = in->clock ? (int16_t)in->min : -1;

    /* a new night */
    if (in->sunset || (in->clock && crossed(last, in->min, SH_COOL_NEW_NIGHT))) {
        c->done = false;
        c->heater = false;
    }
    if (in->sunset) {
        c->watch = false;               /* the day after a cooling night is over */
    }

    /* E4: for b), and for the learning */
    if (in->e4 && !c->e4_prev) {
        c->e4_day = true;
        if (c->watch && c->why == SH_COOL_NONE) {
            c->watch = false;
            learn->offset = (int16_t)(learn->offset - SH_COOL_LEARN_DOWN);
            if (learn->offset < SH_COOL_LEARN_MIN) {
                learn->offset = SH_COOL_LEARN_MIN;
            }
            learn->ok_days = 0;
            c->events |= SH_COOL_EVT_LEARNED;
        }
    }
    c->e4_prev = in->e4;
    if (in->sunrise || (in->clock && crossed(last, in->min, SH_COOL_UNTIL))) {
        c->e4_seen = false;             /* the window of b) is over */
    }
    if (in->e4) {
        c->e4_seen = true;
    }
    if (in->sunrise) {
        c->done = true;                 /* no a) / b) after the sunrise */
    }

    const bool valid = in->t_col != SH_TEMP_INVALID && in->t_tank != SH_TEMP_INVALID;
    const bool vac_night = sh_cool_vacation_night(in);

    /* the running session */
    if (c->why != SH_COOL_NONE) {
        if (!valid) {
            end(c, SH_COOL_END_SENSOR);
        } else if (heater(c, in->t_tank, now_s)) {
            end(c, SH_COOL_END_HEATER);
        } else if (c->why == SH_COOL_VACATION) {
            if (!in->vacation) {
                end(c, SH_COOL_END_MODE);
            } else if (!vac_night || in->sunrise) {
                end(c, SH_COOL_END_SUNRISE);
            }
        } else if (in->vacation) {
            end(c, SH_COOL_END_MODE);   /* the vacation night takes over */
        } else if (in->t_tank <= c->target) {
            end(c, SH_COOL_END_TARGET);
        } else if (in->sunrise || (!in->clock && !in->night)) {
            end(c, SH_COOL_END_SUNRISE);
        } else if (in->clock && !in_window(in->min, SH_COOL_E4_FROM)) {
            end(c, SH_COOL_END_MORNING);
        }
    }

    /* a new session */
    if (c->why == SH_COOL_NONE && valid && !c->heater) {
        if (in->vacation) {
            if (vac_night && !in->sunrise) {
                start(c, in, SH_COOL_VACATION, now_s);
            }
        } else if (!c->done && in->t_tank > in->target) {
            const bool b = c->e4_seen;
            const bool a = in->night_cool && in->tank_max;
            if (in->clock) {
                if (b && in_window(in->min, SH_COOL_E4_FROM)) {
                    start(c, in, SH_COOL_E4, now_s);
                } else if (a && in_window(in->min, SH_COOL_TMAX_FROM)) {
                    start(c, in, SH_COOL_TMAX, now_s);
                }
            } else if (in->night && now_s - in->sunset_s >= SH_COOL_AFTER_SUNSET_S && (a || b)) {
                start(c, in, b ? SH_COOL_E4 : SH_COOL_TMAX, now_s);
            }
        }
    }
}

void sh_cool_day(sh_cool_t *c, sh_cool_learn_t *learn)
{
    if (c->e4_day) {
        learn->ok_days = 0;
    } else if (++learn->ok_days >= SH_COOL_LEARN_DAYS) {
        learn->ok_days = 0;
        if (learn->offset < 0) {
            learn->offset = (int16_t)(learn->offset + SH_COOL_LEARN_UP);
            if (learn->offset > 0) {
                learn->offset = 0;
            }
        }
    }
    c->e4_day = c->e4_prev;             /* an E4 that goes on counts for the new day too */
}

const char *sh_cool_why_name(sh_cool_why_t why)
{
    static const char *const names[] = {
        [SH_COOL_NONE] = "none", [SH_COOL_TMAX] = "tank above Tmax", [SH_COOL_E4] = "E4 during the day",
        [SH_COOL_VACATION] = "vacation",
    };
    return (unsigned)why <= SH_COOL_VACATION ? names[why] : "?";
}

const char *sh_cool_end_name(sh_cool_end_t end)
{
    static const char *const names[] = {
        [SH_COOL_END_NONE] = "-", [SH_COOL_END_TARGET] = "target reached", [SH_COOL_END_SUNRISE] = "sunrise",
        [SH_COOL_END_MORNING] = "07:00", [SH_COOL_END_HEATER] = "heater running (tank rises)",
        [SH_COOL_END_MODE] = "vacation switched", [SH_COOL_END_SENSOR] = "sensor fault",
    };
    return (unsigned)end <= SH_COOL_END_SENSOR ? names[end] : "?";
}
