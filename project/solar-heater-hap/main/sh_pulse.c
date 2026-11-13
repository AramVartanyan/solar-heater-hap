/* The pulse mode: sensors on the pipes — see sh_pulse.h. */

#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_pulse.h"

#define DAY_MIN         1440

bool sh_pulse_fresh(bool pump_on, uint32_t run_s)
{
    return pump_on && run_s >= SH_PULSE_S;
}

uint32_t sh_pulse_day_interval(int16_t t_col)
{
    if (t_col == SH_TEMP_INVALID || t_col < SH_PULSE_WARM) {
        return SH_PULSE_COLD_S;
    }
    return t_col <= SH_PULSE_HOT ? SH_PULSE_WARM_S : SH_PULSE_HOT_S;
}

uint32_t sh_pulse_frost_interval(int32_t reserve)
{
    if (reserve <= SH_PULSE_FROST_NEAR) {
        return SH_PULSE_FROST_NEAR_S;
    }
    return reserve <= SH_PULSE_FROST_MID ? SH_PULSE_FROST_MID_S : SH_PULSE_FROST_FAR_S;
}

static bool learned(int rise, int set)
{
    return rise >= 0 && set >= 0 && rise < set;
}

bool sh_pulse_window(bool reliable, uint16_t min, int rise, int set, bool day)
{
    if (!reliable) {
        return day;
    }
    if (learned(rise, set)) {
        return min >= rise && min < set;
    }
    return min >= SH_PULSE_FROM_MIN && min < SH_PULSE_TO_MIN;
}

void sh_pulse_measure(sh_pulse_t *p, const sh_pulse_in_t *in, uint32_t now_s)
{
    p->events = 0;
    if (in->pipes != p->pipes) {
        /* the mode was switched: the reliable values and the timers of the pulses start over */
        memset(p, 0, sizeof(*p));
        p->pipes = in->pipes;
        p->col = p->tank = SH_TEMP_INVALID;
        p->meas_s = now_s;
        p->events = SH_PULSE_EVT_MODE;
    }
    p->fresh = p->pipes && sh_pulse_fresh(in->pump_on, in->run_s);
    if (!p->fresh) {
        p->was_fresh = false;
        return;
    }

    bool taken = false;
    if (in->t_col != SH_TEMP_INVALID) {
        p->col = in->t_col;
        taken = true;
        p->frost_now = false;
        p->unblocked = false;
        /* overheat: by the last reliable value — a run that cools the collector lifts it again */
        if (in->t_col >= in->boiling - SH_PULSE_BOIL_MARGIN) {
            if (!p->block) {
                p->events |= SH_PULSE_EVT_BLOCK;
            }
            p->block = true;
            p->block_s = now_s;
        } else if (p->block) {
            p->block = false;
            p->events |= SH_PULSE_EVT_UNBLOCK;
        }
    }
    if (in->t_tank != SH_TEMP_INVALID) {
        p->tank = in->t_tank;
        taken = true;
    }
    if (taken) {
        p->have = true;
        p->meas_s = now_s;
        if (!p->was_fresh) {
            p->events |= SH_PULSE_EVT_MEASURED;
        }
        p->was_fresh = true;
    }
}

bool sh_pulse_plan(sh_pulse_t *p, const sh_pulse_in_t *in, uint32_t now_s)
{
    p->due = false;
    p->next_known = false;
    if (in->sunrise) {
        p->day = true;
    }
    if (in->sunset) {
        p->day = false;
    }
    if (!p->pipes) {
        return false;
    }
    const bool have_learned = in->reliable && learned(in->rise, in->set);
    const bool learned_day = have_learned && in->min >= in->rise && in->min < in->set;

    /* overheat: until the learned sunset; with a reliable clock but nothing learned until 18:00 and
     * at least 5 h; without a reliable clock for 5 h. Decided by the clock of the last hot value;
     * a clock that is lost meanwhile leaves the 5 h. */
    if (p->block) {
        if (in->pump_on) {
            p->block_until = learned_day ? SH_PULSE_UNTIL_SUNSET
                           : in->reliable && !have_learned ? SH_PULSE_UNTIL_FIXED : SH_PULSE_UNTIL_TIME;
        } else {
            if (!in->reliable || (p->block_until == SH_PULSE_UNTIL_SUNSET && !have_learned)) {
                p->block_until = SH_PULSE_UNTIL_TIME;
            }
            const bool late = now_s - p->block_s >= SH_PULSE_BLOCK_S;
            const bool fixed_day = in->min >= SH_PULSE_FROM_MIN && in->min < SH_PULSE_TO_MIN;
            if (p->block_until == SH_PULSE_UNTIL_SUNSET ? !learned_day
                : p->block_until == SH_PULSE_UNTIL_FIXED ? late && !fixed_day : late) {
                p->block = false;
                p->unblocked = true;
                p->events |= SH_PULSE_EVT_UNBLOCK;
            }
        }
    }

    /* the frost watch: started outside the window by the gate — the collector pipe with the pump
     * stopped (then a pulse at once) or a reliable value below the threshold + 5 K; it lasts until
     * the window opens */
    const bool window = sh_pulse_window(in->reliable, in->min, in->rise, in->set, p->day);
    const int32_t gate = (int32_t)in->frost + SH_PULSE_FROST_GATE;
    if (!in->frost_on || window) {
        p->frost_watch = p->frost_now = false;
    } else if (!p->frost_watch) {
        if (p->col != SH_TEMP_INVALID && p->col < gate) {
            p->frost_watch = true;
        } else if (!in->pump_on && in->t_col != SH_TEMP_INVALID && in->t_col < gate) {
            p->frost_watch = p->frost_now = true;
        }
    }

    /* a running pump is the pulse; a faulty collector sensor has its own probes (sh_pump_ctl) */
    if (in->pump_on || p->block || in->t_col == SH_TEMP_INVALID) {
        return false;
    }

    uint32_t wait = window ? sh_pulse_day_interval(p->col) : SH_PULSE_NIGHT_S;
    if (in->cooling && wait > SH_PULSE_COOL_S) {
        wait = SH_PULSE_COOL_S;
    }
    if (p->unblocked) {
        wait = 0;
    }
    if (p->frost_watch) {
        if (p->frost_now || p->col == SH_TEMP_INVALID || in->t_col <= p->col - SH_PULSE_FROST_EARLY) {
            wait = 0;                           /* the gate / the early pulse: at once */
        } else {
            const uint32_t frost = sh_pulse_frost_interval((int32_t)p->col - in->frost);
            wait = frost < wait ? frost : wait;
        }
    }
    p->due = now_s - p->meas_s >= wait;
    p->next_s = p->due ? now_s : p->meas_s + wait;
    p->next_known = true;

    /* the window of a reliable clock may open before that */
    if (!window && in->reliable && !p->due) {
        const int from = have_learned ? in->rise : SH_PULSE_FROM_MIN;
        const uint32_t opens_s = now_s + (uint32_t)((from - (int)in->min + DAY_MIN) % DAY_MIN) * 60u;
        const uint32_t day_s = p->meas_s + sh_pulse_day_interval(p->col);
        const uint32_t first_s = opens_s > day_s ? opens_s : day_s;
        if (first_s < p->next_s) {
            p->next_s = first_s;
        }
    }
    return p->due;
}
