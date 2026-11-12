/* The day in RAM — see sh_day.h. */

#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_clock_rules.h"     /* SH_DAY_NONE */
#include "sh_day.h"

#define KEY_LOCAL       0x80000000u     /* the interval follows the local time */
#define DAY_S           (SH_DAY_MIN * 60u)

int sh_run_kind(bool pump_on, sh_pump_reason_t reason)
{
    if (!pump_on) {
        return -1;
    }
    switch (reason) {
    case SH_PUMP_REASON_COL_PROTECT:
    case SH_PUMP_REASON_FROST:
        return SH_RUN_PROTECT;
    case SH_PUMP_REASON_TEST:
    case SH_PUMP_REASON_ANTI_SEIZE:
    case SH_PUMP_REASON_TF_PROBE:
    case SH_PUMP_REASON_CF_PROBE:
        return SH_RUN_OTHER;
    default:                    /* difference, minimum run time, charging with a faulty sensor */
        return SH_RUN_CHARGE;
    }
}

void sh_day_init(sh_day_t *d)
{
    memset(d, 0, sizeof(*d));
    d->last_kind = -1;
}

static uint32_t interval_key(const sh_local_t *lt, uint32_t now_s)
{
    if (lt->set) {
        return KEY_LOCAL | ((uint32_t)lt->day * SH_CHART_SLOTS + lt->min / SH_CHART_SLOT_MIN);
    }
    return now_s / SH_CHART_SLOT_S;
}

static void start_interval(sh_day_t *d, const sh_local_t *lt, uint32_t now_s)
{
    d->key = interval_key(lt, now_s);
    d->cur_day = lt->set ? lt->day : SH_DAY_NONE;
    d->cur_idx = lt->set ? (uint8_t)(lt->min / SH_CHART_SLOT_MIN) : 0;
    memset(d->cur_run_s, 0, sizeof(d->cur_run_s));
}

static void close_interval(sh_day_t *d, const sh_day_in_t *in, uint32_t now_s)
{
    const uint16_t i = d->count < SH_CHART_SLOTS ? (uint16_t)((d->head + d->count) % SH_CHART_SLOTS) : d->head;
    sh_chart_slot_t *s = &d->slot[i];
    s->end_s = now_s;
    s->day = d->cur_day;
    s->idx = d->cur_idx;
    s->t_col = in->col_fault ? SH_TEMP_INVALID : in->t_col;
    s->t_tank = in->tank_fault ? SH_TEMP_INVALID : in->t_tank;
    for (int k = 0; k < SH_RUN_KINDS; k++) {
        s->run_s[k] = (uint16_t)(d->cur_run_s[k] < 0xFFFFu ? d->cur_run_s[k] : 0xFFFFu);
    }
    if (d->count < SH_CHART_SLOTS) {
        d->count++;
    } else {
        d->head = (uint16_t)((d->head + 1) % SH_CHART_SLOTS);   /* the oldest is overwritten */
    }
}

static void start_sum(sh_day_sum_t *s, uint16_t day, bool full)
{
    memset(s, 0, sizeof(*s));
    s->valid = true;
    s->full = full;
    s->day = day;
    s->col_max = s->col_min = s->tank_max = s->tank_min = SH_TEMP_INVALID;
    s->peak_min = -1;
}

static void take_temps(sh_day_sum_t *s, const sh_day_in_t *in, uint16_t min)
{
    if (in->col_fault) {
        s->col_fault = true;
    } else if (in->t_col != SH_TEMP_INVALID) {
        if (s->col_max == SH_TEMP_INVALID || in->t_col > s->col_max) {
            s->col_max = in->t_col;
            s->peak_min = (int16_t)min;
        }
        if (s->col_min == SH_TEMP_INVALID || in->t_col < s->col_min) {
            s->col_min = in->t_col;
        }
    }
    if (!in->tank_fault && in->t_tank != SH_TEMP_INVALID) {
        if (s->tank_max == SH_TEMP_INVALID || in->t_tank > s->tank_max) {
            s->tank_max = in->t_tank;
        }
        if (s->tank_min == SH_TEMP_INVALID || in->t_tank < s->tank_min) {
            s->tank_min = in->t_tank;
        }
    }
    s->alarms |= in->alarms;
}

bool sh_day_step(sh_day_t *d, const sh_day_in_t *in, const sh_local_t *lt, uint32_t now_s,
                 sh_day_sum_t *done)
{
    bool over = false;
    const int kind = sh_run_kind(in->pump_on, in->reason);

    if (!d->started) {
        d->started = true;
        start_interval(d, lt, now_s);
    } else {
        /* the time since the previous step, with the pump as it was then */
        const uint32_t dt = now_s - d->last_s;
        if (d->last_kind >= 0) {
            d->cur_run_s[d->last_kind] += dt;
            if (d->sum.valid) {
                d->sum.run_s[d->last_kind] += dt;
            }
        }
        if (interval_key(lt, now_s) != d->key) {
            close_interval(d, in, now_s);
            start_interval(d, lt, now_s);
        }
    }

    if (lt->set) {
        if (!d->sum.valid) {
            start_sum(&d->sum, lt->day, false);         /* the clock was set during the day */
        } else if (lt->day != d->sum.day) {
            const bool midnight = lt->day == d->sum.day + 1 && lt->min < 60;
            if (midnight) {
                *done = d->sum;
                over = true;
            }
            start_sum(&d->sum, lt->day, midnight);
        }
        take_temps(&d->sum, in, lt->min);
    } else {
        d->sum.valid = false;
    }

    d->last_s = now_s;
    d->last_kind = kind;
    return over;
}

/* Is the slot on the chart now? */
static bool on_chart(const sh_chart_slot_t *s, const sh_local_t *lt, uint32_t now_s)
{
    if (lt->set) {
        return s->day == lt->day;
    }
    return now_s - s->end_s < DAY_S - SH_CHART_SLOT_S;
}

void sh_day_run_today(const sh_day_t *d, const sh_local_t *lt, uint32_t now_s,
                      uint32_t run_s[SH_RUN_KINDS])
{
    memset(run_s, 0, SH_RUN_KINDS * sizeof(run_s[0]));
    if (lt->set) {
        if (d->sum.valid && d->sum.day == lt->day) {
            memcpy(run_s, d->sum.run_s, SH_RUN_KINDS * sizeof(run_s[0]));
        }
        return;
    }
    for (uint16_t n = 0; n < d->count; n++) {
        const sh_chart_slot_t *s = &d->slot[(d->head + n) % SH_CHART_SLOTS];
        if (on_chart(s, lt, now_s)) {
            for (int k = 0; k < SH_RUN_KINDS; k++) {
                run_s[k] += s->run_s[k];
            }
        }
    }
    if (d->started) {
        for (int k = 0; k < SH_RUN_KINDS; k++) {
            run_s[k] += d->cur_run_s[k];
        }
    }
}

unsigned sh_day_chart(const sh_day_t *d, const sh_local_t *lt, uint32_t now_s, sh_chart_fn fn, void *ctx)
{
    unsigned n = 0;
    for (uint16_t i = 0; i < d->count; i++) {
        const sh_chart_slot_t *s = &d->slot[(d->head + i) % SH_CHART_SLOTS];
        if (on_chart(s, lt, now_s)) {
            fn(ctx, s);
            n++;
        }
    }
    return n;
}
