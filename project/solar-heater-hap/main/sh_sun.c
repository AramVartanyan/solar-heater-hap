/* Sunrise and sunset seen by the collector — see sh_sun.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_clock_rules.h"     /* SH_DAY_NONE */
#include "sh_sun.h"

void sh_sun_days_reset(sh_sun_days_t *d)
{
    for (int i = 0; i < SH_SUN_DAYS; i++) {
        d->rise[i] = SH_SUN_NONE;
        d->set[i] = SH_SUN_NONE;
    }
}

int sh_sun_avg(const uint16_t v[SH_SUN_DAYS])
{
    long sum = 0;
    int n = 0;
    for (int i = 0; i < SH_SUN_DAYS; i++) {
        if (v[i] != SH_SUN_NONE) {
            sum += v[i];
            n++;
        }
    }
    return n ? (int)((sum + n / 2) / n) : -1;
}

static void days_add(uint16_t v[SH_SUN_DAYS], uint16_t min)
{
    for (int i = 0; i < SH_SUN_DAYS - 1; i++) {
        v[i] = v[i + 1];
    }
    v[SH_SUN_DAYS - 1] = min;
}

uint8_t sh_sun_step(sh_sun_t *s, int16_t t_col, int16_t t_tank, bool pump_on, const sh_local_t *lt,
                    bool reliable, uint32_t now_s)
{
    return sh_sun_step_flow(s, t_col, t_tank, pump_on, false, lt, reliable, now_s);
}

uint8_t sh_sun_step_flow(sh_sun_t *s, int16_t t_col, int16_t t_tank, bool pump_on, bool flow,
                         const sh_local_t *lt, bool reliable, uint32_t now_s)
{
    uint8_t evt = 0;

    /* the last sunset of a finished day becomes a learned one */
    if (s->set_pending && (!lt->set || lt->day != s->set_day)) {
        days_add(s->days.set, s->set_min);
        s->set_pending = false;
        evt |= SH_SUN_EVT_LEARNED;
    }
    /* a new local day: its sunrise is still to come */
    if (lt->set && s->rise_done && s->rise_day != lt->day) {
        s->rise_done = false;
    }

    /* the collector with the pump stopped, a sample a minute; a running pump or no value restarts */
    bool rise = false;
    if (pump_on || t_col == SH_TEMP_INVALID) {
        s->n = 0;
        s->head = 0;
        s->next_s = now_s;
    } else if (now_s >= s->next_s) {
        s->hist[s->head] = t_col;
        s->head = (uint8_t)((s->head + 1) % SH_SUN_RISE_SAMPLES);
        if (s->n < SH_SUN_RISE_SAMPLES) {
            s->n++;
        }
        s->next_s = now_s + 60;
        /* full ring: head is the sample of 10 min ago */
        rise = s->n == SH_SUN_RISE_SAMPLES && t_col - s->hist[s->head] >= SH_SUN_RISE_K;
    }
    if (rise && !s->rise_done) {
        s->rise_done = true;
        s->rise_day = lt->set ? lt->day : SH_DAY_NONE;
        if (!lt->set || lt->min < SH_SUN_RISE_BEFORE_MIN) {
            s->dark = false;
            s->risen = true;
            evt |= SH_SUN_EVT_RISE;
            if (lt->set && reliable) {
                days_add(s->days.rise, lt->min);
                evt |= SH_SUN_EVT_LEARNED;
            }
        }
    }

    if (t_col != SH_TEMP_INVALID && t_tank != SH_TEMP_INVALID) {
        if (t_col > t_tank) {
            s->above = true;
            s->dark = false;            /* day */
        } else if (s->above && (!pump_on || flow) && t_col < t_tank) {
            s->above = false;
            s->dark = true;
            s->risen = false;
            s->sunset_s = now_s;
            if (!lt->set) {
                s->rise_done = false;   /* without a clock: the next rise is the sunrise */
            }
            evt |= SH_SUN_EVT_SET;
            if (lt->set && reliable) {
                s->set_pending = true;  /* a later one of the same day replaces it */
                s->set_day = lt->day;
                s->set_min = lt->min;
            }
        }
    }
    return evt;
}

bool sh_sun_night(const sh_sun_t *s, const sh_local_t *lt, bool reliable)
{
    const int rise = sh_sun_avg(s->days.rise);
    const int set = sh_sun_avg(s->days.set);
    if (lt->set && reliable && rise >= 0 && set >= 0 && rise < set) {
        return lt->min >= set || lt->min < rise;
    }
    return s->dark;
}
