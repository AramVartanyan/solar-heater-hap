/* E8: reverse thermosiphon — see sh_e8.h. */

#include <stdlib.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_e8.h"

int16_t sh_e8_threshold(bool tubes, int16_t area, int16_t volume_l)
{
    if (area <= 0 || volume_l <= 0) {
        return 0;
    }
    /* W = loss[0.1 W/m²K] / 10 x area[0.1 m²] / 10 x 40 K; rate[0.1 K/h] = W x 10 / (l x 1.163);
     * half of it, rounded up */
    const int32_t num = (int32_t)(tubes ? SH_E8_TUBES_LOSS : SH_E8_FLAT_LOSS) * area * SH_E8_DT_K * 100;
    const int32_t den = (int32_t)volume_l * 1163 * 2;
    return (int16_t)((num + den - 1) / den);
}

void sh_e8_learned_reset(sh_e8_learned_t *l)
{
    for (int i = 0; i < SH_E8_DAYS; i++) {
        l->rate[i] = SH_E8_NO_RATE;
    }
    l->sign_nights = 0;
    l->normal_nights = 0;
    l->active = false;
}

int16_t sh_e8_normal(const sh_e8_learned_t *l)
{
    int32_t sum = 0;
    int n = 0;
    for (int i = 0; i < SH_E8_DAYS; i++) {
        if (l->rate[i] != SH_E8_NO_RATE) {
            sum += l->rate[i];
            n++;
        }
    }
    if (n == 0) {
        return SH_E8_NO_RATE;
    }
    return (int16_t)(sum >= 0 ? (sum + n / 2) / n : -((-sum + n / 2) / n));
}

static void add_rate(sh_e8_learned_t *l, int16_t rate)
{
    for (int i = 0; i < SH_E8_DAYS - 1; i++) {
        l->rate[i] = l->rate[i + 1];
    }
    l->rate[SH_E8_DAYS - 1] = rate;
}

static void night_start(sh_e8_t *e)
{
    e->sign = false;
    e->hour = false;
    e->fall_sum = 0;
    e->fall_min = 0;
    e->fall_last_ok = false;
}

/* The night is over: is it one with the sign, a normal one, or neither? */
static void night_end(sh_e8_t *e)
{
    sh_e8_learned_t *l = &e->l;
    if (e->sign) {
        l->normal_nights = 0;
        if (l->sign_nights < 255) {
            l->sign_nights++;
        }
        if (l->sign_nights >= SH_E8_NIGHTS) {
            l->active = true;
        }
    } else if (e->hour) {
        l->sign_nights = 0;
        if (l->normal_nights < 255) {
            l->normal_nights++;
        }
        if (l->normal_nights >= SH_E8_NIGHTS) {
            l->active = false;
        }
        if (e->fall_min >= 60) {
            const int32_t num = e->fall_sum * 60, den = e->fall_min;
            add_rate(l, (int16_t)(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den)));
        }
    } else {
        return;                     /* not counted */
    }
    e->events |= SH_E8_EVT_NIGHT | SH_E8_EVT_CHANGED;
}

static void restart(sh_e8_t *e, uint32_t now_s)
{
    e->n = 0;
    e->head = 0;
    e->next_s = now_s;
    e->fall_last_ok = false;
}

void sh_e8_step(sh_e8_t *e, const sh_e8_in_t *in, uint32_t now_s)
{
    e->events = 0;
    if (in->night && !e->was_night) {
        night_start(e);
    } else if (!in->night && e->was_night) {
        night_end(e);
    }
    e->was_night = in->night;

    if (!in->night || in->pump_on || in->t_col == SH_TEMP_INVALID || in->t_tank == SH_TEMP_INVALID) {
        restart(e, now_s);
        return;
    }
    if (now_s < e->next_s) {
        return;
    }
    e->next_s = now_s + 60;

    /* the normal cooling: the tank fall minute by minute, without the heater */
    if (in->heater) {
        e->fall_last_ok = false;
    } else {
        if (e->fall_last_ok && e->fall_min < 0xFFFF) {
            e->fall_sum += e->fall_last - in->t_tank;
            e->fall_min++;
        }
        e->fall_last = in->t_tank;
        e->fall_last_ok = true;
    }

    e->col[e->head] = in->t_col;
    e->tank[e->head] = in->t_tank;
    e->head = (uint8_t)((e->head + 1) % SH_E8_SAMPLES);
    if (e->n < SH_E8_SAMPLES) {
        e->n++;
    }
    if (e->n < SH_E8_SAMPLES) {
        return;                     /* not 1 h yet */
    }
    /* full ring: head is the sample of 1 h ago */
    const int16_t col0 = e->col[e->head], tank0 = e->tank[e->head];
    const int32_t col_rise = (int32_t)in->t_col - col0;
    const int32_t tank_fall = (int32_t)tank0 - in->t_tank;
    const int16_t normal = sh_e8_normal(&e->l);
    const bool sign = col_rise >= SH_E8_COL_RISE && tank_fall >= in->threshold &&
                      (normal == SH_E8_NO_RATE || tank_fall > normal) &&
                      abs(in->t_col - in->t_tank) < abs(col0 - tank0);
    e->last_rate = (int16_t)tank_fall;
    if (sign) {
        if (!e->sign) {
            e->events |= SH_E8_EVT_SIGN;
        }
        e->sign = true;
    } else {
        e->hour = true;
    }
}
