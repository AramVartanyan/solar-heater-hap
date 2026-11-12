/* The heater: sign, detection, guaranteed daylight, leak checks — see sh_heater.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_heater.h"

int16_t sh_heater_rise(int16_t power, int16_t volume_l)
{
    if (power <= 0 || volume_l <= 0) {
        return SH_HEATER_MIN_RISE;
    }
    /* half of the rise in 10 min, tenths of K: power[0.1 kW] x 100 W x 10 / (l x 1.163 x 6 x 2),
     * rounded up ("at least half") */
    const int32_t num = (int32_t)power * 1000000;
    const int32_t den = (int32_t)volume_l * 13956;
    const int32_t rise = (num + den - 1) / den;
    return (int16_t)(rise > SH_HEATER_MIN_RISE ? rise : SH_HEATER_MIN_RISE);
}

bool sh_heater_step(sh_heater_t *h, int16_t power, bool sign)
{
    if (power == 0 && sign && !h->detected) {
        h->detected = true;
        return true;
    }
    return false;
}

bool sh_heater_present(const sh_heater_t *h, int16_t power)
{
    return power > 0 || h->detected;
}

bool sh_daylight(int rise, int set, uint16_t min)
{
    int from = SH_DAYLIGHT_FROM_MIN, to = SH_DAYLIGHT_TO_MIN;
    if (rise >= 0 && set >= 0) {
        from = rise + SH_DAYLIGHT_MARGIN_MIN;
        to = set - SH_DAYLIGHT_MARGIN_MIN;
    }
    return min >= from && min < to;
}

/* Charging time before a check with a reliable clock. */
static uint32_t check_after(const sh_leak_in_t *in)
{
    if (in->set >= 0 && in->min >= in->set - SH_LEAK_EVENING_MIN && in->min < in->set) {
        return SH_LEAK_EVENING_S;
    }
    return SH_LEAK_EVERY_S;
}

static void start_check(sh_leak_t *l, int16_t t_col, uint32_t now_s)
{
    l->phase = SH_LEAK_CHECK;
    l->check_s = now_s;
    l->check_ref = t_col;
    l->counting = false;
    l->fall_n = 0;
    l->events |= SH_LEAK_EVT_CHECK;
}

static void to_idle(sh_leak_t *l)
{
    l->phase = SH_LEAK_IDLE;
    l->counting = false;
    l->fall_n = 0;
}

/* Idle: is a check due? */
static void idle(sh_leak_t *l, const sh_leak_in_t *in, uint32_t now_s)
{
    if (!in->charging || in->t_col == SH_TEMP_INVALID) {
        l->counting = false;
        l->fall_n = 0;
        return;
    }
    if (in->reliable) {
        l->fall_n = 0;
        if (sh_daylight(in->rise, in->set, in->min)) {
            l->counting = false;
            return;
        }
        if (!l->counting) {
            l->counting = true;
            l->run_s = now_s;
            l->every_s = check_after(in);
        }
        if (now_s - l->run_s >= l->every_s) {
            start_check(l, in->t_col, now_s);
        }
        return;
    }
    /* without a reliable clock: 30 min in a row in which the collector fell */
    l->counting = false;
    if (l->fall_n == 0) {
        l->fall_n = 1;
        l->fall_first = l->fall_last = in->t_col;
        l->fall_next_s = now_s + 60;
        return;
    }
    if (now_s < l->fall_next_s) {
        return;
    }
    l->fall_next_s = now_s + 60;
    if (in->t_col > l->fall_last) {
        l->fall_n = 1;                          /* a rise: the count starts again */
        l->fall_first = l->fall_last = in->t_col;
        return;
    }
    l->fall_last = in->t_col;
    if (l->fall_n < 255) {
        l->fall_n++;
    }
    if (l->fall_n > SH_LEAK_FALL_MIN && in->t_col < l->fall_first) {
        start_check(l, in->t_col, now_s);
    }
}

void sh_leak_step(sh_leak_t *l, const sh_leak_in_t *in, uint32_t now_s)
{
    l->events = 0;
    l->resume = false;

    if (!in->heater) {
        if (l->phase == SH_LEAK_CHECK) {
            l->events |= SH_LEAK_EVT_ABORT;
        } else if (l->phase == SH_LEAK_STOP) {
            l->events |= SH_LEAK_EVT_END;
        }
        to_idle(l);
        return;
    }

    switch (l->phase) {
    case SH_LEAK_IDLE:
        idle(l, in, now_s);
        break;

    case SH_LEAK_CHECK:
        /* the pump was stopped in the step that started the check: running now means that something
         * else runs it (a protection, the test ...) */
        if (in->t_col == SH_TEMP_INVALID || (in->pump_on && now_s != l->check_s)) {
            l->events |= SH_LEAK_EVT_ABORT;
            to_idle(l);
            break;
        }
        if (now_s - l->check_s >= SH_LEAK_CHECK_S) {
            const int16_t drop = (int16_t)(l->check_ref - in->t_col);
            l->last = true;
            l->last_drop = drop;
            l->last_stop = drop >= SH_LEAK_DROP;
            if (l->last_stop) {
                l->phase = SH_LEAK_STOP;
                l->warm_n = 0;
                l->warm_head = 0;
                l->warm_next_s = now_s;
                l->events |= SH_LEAK_EVT_STOP;
            } else {
                to_idle(l);
                l->resume = true;
                l->events |= SH_LEAK_EVT_PASS;
                l->counting = in->reliable;     /* the charging time counts from now */
                l->run_s = now_s;
                l->every_s = check_after(in);
            }
        }
        break;

    case SH_LEAK_STOP:
        if (in->sunrise) {
            l->events |= SH_LEAK_EVT_END;
            to_idle(l);
            break;
        }
        /* the collector warms by itself: >= 2 K in 10 min, all with the pump stopped */
        if (in->pump_on || in->t_col == SH_TEMP_INVALID) {
            l->warm_n = 0;
            l->warm_head = 0;
            l->warm_next_s = now_s;
        } else if (now_s >= l->warm_next_s) {
            l->warm[l->warm_head] = in->t_col;
            l->warm_head = (uint8_t)((l->warm_head + 1) % SH_LEAK_WARM_SAMPLES);
            if (l->warm_n < SH_LEAK_WARM_SAMPLES) {
                l->warm_n++;
            }
            l->warm_next_s = now_s + 60;
            /* full ring: warm_head is the sample of 10 min ago */
            if (l->warm_n == SH_LEAK_WARM_SAMPLES && in->t_col - l->warm[l->warm_head] >= SH_LEAK_WARM) {
                l->events |= SH_LEAK_EVT_END;
                to_idle(l);
            }
        }
        break;
    }
}

uint32_t sh_leak_left(const sh_leak_t *l, uint32_t now_s)
{
    if (l->phase != SH_LEAK_CHECK) {
        return 0;
    }
    const uint32_t t = now_s - l->check_s;
    return t < SH_LEAK_CHECK_S ? SH_LEAK_CHECK_S - t : 0;
}
