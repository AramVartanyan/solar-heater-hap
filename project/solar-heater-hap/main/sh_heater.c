/* The heater of the tank: its sign and the heat leak through the pump — see sh_heater.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_heater.h"

int16_t sh_heater_rise(int16_t power, int16_t volume_l)
{
    if (power <= 0 || volume_l <= 0) {
        return SH_HEATER_RISE_MIN;
    }
    /* W = power x 100; K in 10 min = W / 6 / (l x 1.163); half of it, in tenths of K:
     * power x 100 x 10 / (12 x l x 1.163) = power x 1 000 000 / (13 956 x l), rounded */
    const int32_t per = (int32_t)volume_l * 13956;
    const int32_t rise = ((int32_t)power * 1000000 + per / 2) / per;
    return (int16_t)(rise < SH_HEATER_RISE_MIN ? SH_HEATER_RISE_MIN : rise);
}

bool sh_heater_present(int16_t power, bool found)
{
    return power > 0 || found;
}

bool sh_heater_found(bool *found, int16_t power, bool sign)
{
    if (power == 0 && sign && !*found) {
        *found = true;
        return true;
    }
    return false;
}

bool sh_leak_daylight(uint16_t min, int rise, int set)
{
    int from = SH_LEAK_FROM_MIN, to = SH_LEAK_TO_MIN;
    if (rise >= 0 && set >= 0 && rise + SH_LEAK_MARGIN_MIN < set - SH_LEAK_MARGIN_MIN) {
        from = rise + SH_LEAK_MARGIN_MIN;
        to = set - SH_LEAK_MARGIN_MIN;
    }
    return min >= from && min < to;
}

static void idle(sh_leak_t *l)
{
    l->state = SH_LEAK_IDLE;
    l->counting = false;
    l->falling = false;
}

/* Is a check due? Called while the pump charges by the difference. */
static bool check_due(sh_leak_t *l, const sh_leak_in_t *in, uint32_t now_s)
{
    if (in->reliable) {
        l->falling = false;
        if (sh_leak_daylight(in->min, in->rise, in->set)) {
            l->counting = false;
            return false;
        }
        if (!l->counting) {
            /* the time counts from here; the evening is judged at this moment */
            l->counting = true;
            l->since_s = now_s;
            l->evening = in->set >= 0 && in->min < in->set && in->min >= in->set - SH_LEAK_EVENING_MIN;
        }
        return now_s - l->since_s >= (l->evening ? SH_LEAK_EVENING_S : SH_LEAK_PERIOD_S);
    }

    /* no reliable clock: the collector, a sample a minute; a sample above the one before it ends
     * the fall */
    l->counting = false;
    if (!l->falling) {
        l->falling = true;
        l->fall_s = now_s;
        l->fall_ref = l->fall_prev = in->t_col;
        l->fall_next_s = now_s + 60;
    } else if (now_s >= l->fall_next_s) {
        l->fall_next_s = now_s + 60;
        if (in->t_col > l->fall_prev) {
            l->fall_s = now_s;
            l->fall_ref = in->t_col;
        }
        l->fall_prev = in->t_col;
    }
    return now_s - l->fall_s >= SH_LEAK_FALL_S && in->t_col < l->fall_ref;
}

void sh_leak_step(sh_leak_t *l, const sh_leak_in_t *in, uint32_t now_s)
{
    l->events = 0;
    l->resume = false;

    if (!l->block && in->pump_on) {
        l->info = false;                /* the pump has started again */
    }

    if (!in->heater) {
        /* no heater: nothing to check, nothing stays blocked */
        if (l->state == SH_LEAK_CHECK) {
            l->events |= SH_LEAK_EVT_ABORT;
        } else if (l->state == SH_LEAK_STOPPED) {
            l->events |= SH_LEAK_EVT_RELEASED;
        }
        idle(l);
        l->info = false;
    } else if (l->state == SH_LEAK_CHECK) {
        if (in->pump_on || in->t_col == SH_TEMP_INVALID) {
            /* a protection (or the test ...) runs the pump, or the collector is not known */
            idle(l);
            l->events |= SH_LEAK_EVT_ABORT;
        } else if (now_s - l->check_s >= SH_LEAK_CHECK_S) {
            l->drop = (int16_t)(l->check_ref - in->t_col);
            l->last_s = now_s;
            idle(l);
            if (l->drop >= SH_LEAK_DROP) {
                l->state = SH_LEAK_STOPPED;
                l->last = SH_LEAK_RES_LEAK;
                l->info = true;
                l->events |= SH_LEAK_EVT_LEAK;
            } else {
                l->last = SH_LEAK_RES_SUN;
                l->resume = true;
                l->events |= SH_LEAK_EVT_SUN;
            }
        }
    } else if (l->state == SH_LEAK_STOPPED) {
        if (in->warming || in->sunrise ||
            (in->reliable && sh_leak_daylight(in->min, in->rise, in->set))) {
            idle(l);
            l->events |= SH_LEAK_EVT_RELEASED;
        }
    } else if (!in->charging || in->t_col == SH_TEMP_INVALID) {
        l->counting = false;            /* a break: the time starts over */
        l->falling = false;
    } else if (check_due(l, in, now_s)) {
        idle(l);
        l->state = SH_LEAK_CHECK;
        l->check_s = now_s;
        l->check_ref = in->t_col;
        l->events |= SH_LEAK_EVT_CHECK;
    }

    l->hold = l->state == SH_LEAK_CHECK;
    l->block = l->state == SH_LEAK_STOPPED;
}
