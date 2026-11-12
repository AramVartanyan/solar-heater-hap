/* E8 — reverse thermosiphon — see sh_e8.h. */

#include <stdlib.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_e8.h"

#define SAMPLES     (SH_E8_WINDOW_MIN + 1)

int16_t sh_e8_fall(bool tubes, int16_t area, int16_t volume_l)
{
    if (area <= 0 || volume_l <= 0) {
        return SH_E8_FALL_MIN;
    }
    /* W = loss x area x 40 K (both in tenths: / 100); K/h = W / (l x 1.163); half of it, in
     * tenths of K/h: loss x area x 40 x 10 x 1000 / (100 x 2 x l x 1163), rounded */
    const int32_t per = (int32_t)volume_l * 1163;
    const int32_t w2 = (int32_t)(tubes ? SH_E8_TUBES_W_M2K : SH_E8_FLAT_W_M2K) * area * SH_E8_NIGHT_DIFF_K * 50;
    const int32_t fall = (w2 + per / 2) / per;
    return (int16_t)(fall < SH_E8_FALL_MIN ? SH_E8_FALL_MIN : fall);
}

void sh_e8_keep_reset(sh_e8_keep_t *k)
{
    k->active = false;
    k->sign_nights = 0;
    k->normal_nights = 0;
    for (int i = 0; i < SH_E8_LEARN_NIGHTS; i++) {
        k->rate[i] = SH_E8_NO_RATE;
    }
}

int sh_e8_normal(const sh_e8_keep_t *k)
{
    int sum = 0, n = 0;
    for (int i = 0; i < SH_E8_LEARN_NIGHTS; i++) {
        if (k->rate[i] != SH_E8_NO_RATE) {
            sum += k->rate[i];
            n++;
        }
    }
    return n ? (sum + n / 2) / n : -1;
}

static void night_start(sh_e8_t *e)
{
    e->sign = false;
    e->judged = false;
    e->n = 0;
    e->head = 0;
    e->have_prev = false;
    e->fall_sum = 0;
    e->fall_min = 0;
}

static void night_end(sh_e8_t *e)
{
    sh_e8_keep_t *k = &e->keep;
    if (e->sign) {
        return;                         /* counted when the sign appeared */
    }
    if (!e->judged) {
        e->events |= SH_E8_EVT_SKIPPED;
        return;
    }
    /* a normal night: its cooling is learned, the series of sign nights is broken */
    if (e->fall_min > 0) {
        int32_t rate = (e->fall_sum * 60 + e->fall_min / 2) / e->fall_min;
        rate = rate < 0 ? 0 : rate > SH_E8_RATE_MAX ? SH_E8_RATE_MAX : rate;
        for (int i = 0; i < SH_E8_LEARN_NIGHTS - 1; i++) {
            k->rate[i] = k->rate[i + 1];
        }
        k->rate[SH_E8_LEARN_NIGHTS - 1] = (uint8_t)rate;
    }
    k->sign_nights = 0;
    if (k->normal_nights < SH_E8_NIGHTS) {
        k->normal_nights++;
    }
    if (k->active && k->normal_nights >= SH_E8_NIGHTS) {
        k->active = false;
        e->events |= SH_E8_EVT_OFF;
    }
    e->events |= SH_E8_EVT_NORMAL | SH_E8_EVT_STORE;
}

/* The sign over the last hour: the ring is full, `head` is the sample of an hour ago. */
static bool sign_now(const sh_e8_t *e, const sh_e8_in_t *in)
{
    const int16_t col0 = e->col[e->head], tank0 = e->tank[e->head];
    const int col_rise = in->t_col - col0;
    const int tank_fall = tank0 - in->t_tank;
    const int normal = sh_e8_normal(&e->keep);
    return col_rise >= SH_E8_COL_RISE && tank_fall >= in->fall && (normal < 0 || tank_fall > normal) &&
           abs(in->t_col - in->t_tank) < abs(col0 - tank0);
}

bool sh_e8_step(sh_e8_t *e, const sh_e8_in_t *in, uint32_t now_s)
{
    sh_e8_keep_t *k = &e->keep;
    e->events = 0;

    if (in->night && !e->night) {
        night_start(e);
    } else if (!in->night && e->night) {
        night_end(e);
    }
    e->night = in->night;
    if (!in->night) {
        return k->active;
    }

    /* a sample a minute while the pump is off and both temperatures are valid; anything else
     * starts the hour over */
    if (in->pump_on || in->t_col == SH_TEMP_INVALID || in->t_tank == SH_TEMP_INVALID) {
        e->n = 0;
        e->head = 0;
        e->have_prev = false;
        e->next_s = now_s;
        return k->active;
    }
    if (now_s < e->next_s) {
        return k->active;
    }
    e->next_s = now_s + 60;

    /* the normal cooling of this night: the minutes without the heater */
    if (in->heater) {
        e->have_prev = false;
    } else {
        if (e->have_prev) {
            e->fall_sum += e->prev_tank - in->t_tank;
            e->fall_min++;
        }
        e->have_prev = true;
        e->prev_tank = in->t_tank;
    }

    e->col[e->head] = in->t_col;
    e->tank[e->head] = in->t_tank;
    e->head = (uint8_t)((e->head + 1) % SAMPLES);
    if (e->n < SAMPLES) {
        e->n++;
    }
    if (e->n < SAMPLES) {
        return k->active;               /* the pump has not been off for an hour yet */
    }
    e->judged = true;
    if (!e->sign && sign_now(e, in)) {
        e->sign = true;
        k->normal_nights = 0;
        if (k->sign_nights < SH_E8_NIGHTS) {
            k->sign_nights++;
        }
        if (!k->active && k->sign_nights >= SH_E8_NIGHTS) {
            k->active = true;
            e->events |= SH_E8_EVT_ON;
        }
        e->events |= SH_E8_EVT_SIGN | SH_E8_EVT_STORE;
    }
    return k->active;
}
