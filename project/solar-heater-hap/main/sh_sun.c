/* Sunrise and sunset from the collector — see sh_sun.h. */

#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_sun.h"

void sh_sun_init(sh_sun_t *s)
{
    memset(s, 0, sizeof(*s));
    s->rise_min = s->set_min = SH_SUN_NONE;
    s->done_rise = s->done_set = SH_SUN_NONE;
}

/* One collector sample a minute while the pump is off; a running pump (or no value) starts over. */
static bool risen(sh_sun_t *s, const sh_sun_in_t *in, uint32_t now_s)
{
    if (in->pump_on || in->t_col == SH_TEMP_INVALID) {
        s->n = 0;
        s->next_s = now_s;
        return false;
    }
    if (now_s < s->next_s) {
        return false;
    }
    s->col[s->head] = in->t_col;
    s->head = (uint8_t)((s->head + 1) % SH_SUN_SAMPLES);
    if (s->n < SH_SUN_SAMPLES) {
        s->n++;
    }
    s->next_s = now_s + 60;
    if (s->n < SH_SUN_SAMPLES) {
        return false;
    }
    /* full ring: head is the oldest sample (10 min ago), the one before it the newest */
    const int16_t oldest = s->col[s->head];
    const int16_t newest = s->col[(s->head + SH_SUN_SAMPLES - 1) % SH_SUN_SAMPLES];
    return newest - oldest >= SH_SUN_RISE;
}

uint8_t sh_sun_step(sh_sun_t *s, const sh_sun_in_t *in, uint32_t now_s)
{
    uint8_t evt = 0;

    /* the local date: a new day at the midnight; the date first known (the clock set) is a new day
     * too, but the sunrise is looked for only before noon */
    if (in->clock && (!s->have_day || in->day != s->day)) {
        const bool midnight = s->have_day && in->day == s->day + 1 && in->min < 60;
        if (midnight) {
            s->done_rise = s->rise_min;
            s->done_set = s->set_min;
            evt |= SH_SUN_EVT_DAY;
        }
        s->have_day = true;
        s->day = in->day;
        s->rise_min = s->set_min = SH_SUN_NONE;
        s->armed = midnight || in->min < SH_SUN_NOON;
    }
    if (!in->clock) {
        s->have_day = false;
    }

    const bool rise = risen(s, in, now_s);
    if (in->t_col == SH_TEMP_INVALID || in->t_tank == SH_TEMP_INVALID) {
        return evt;
    }

    if (rise && s->armed) {
        s->armed = false;
        s->night = false;
        if (in->clock) {
            s->rise_min = (int16_t)in->min;
        }
        evt |= SH_SUN_EVT_RISE;
    }

    if (in->t_col > in->t_tank) {
        s->above = true;
        if (!in->pump_on) {
            s->night = false;           /* day again (a cloud was taken for the sunset) */
        }
    } else if (s->above && !in->pump_on && in->t_col < in->t_tank) {
        s->above = false;
        s->night = true;
        s->sunset_s = now_s;
        if (in->clock) {
            s->set_min = (int16_t)in->min;
        } else {
            s->armed = true;            /* without a clock the sunrise is the first one after it */
        }
        evt |= SH_SUN_EVT_SET;
    }
    return evt;
}

void sh_sun_list_add(sh_sun_list_t *l, int16_t min)
{
    if (min < 0) {
        return;
    }
    for (int i = SH_SUN_DAYS - 1; i > 0; i--) {
        l->v[i] = l->v[i - 1];
    }
    l->v[0] = min;
    if (l->n < SH_SUN_DAYS) {
        l->n++;
    }
}

int16_t sh_sun_list_avg(const sh_sun_list_t *l)
{
    if (l->n == 0) {
        return SH_SUN_NONE;
    }
    int32_t sum = 0;
    for (int i = 0; i < l->n; i++) {
        sum += l->v[i];
    }
    return (int16_t)((sum + l->n / 2) / l->n);
}
