/* Alarm and information codes — see sh_alarm.h. */

#include <stddef.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarm.h"

#define SAMPLE_S        60u

/* The pump runs by the normal control: the reasons of sh_pump_decide() with the pump on. */
static bool normal_run(const sh_alarm_in_t *in)
{
    if (!in->pump_on || in->col_fault || in->tank_fault ||
        in->t_col == SH_TEMP_INVALID || in->t_tank == SH_TEMP_INVALID) {
        return false;
    }
    return in->reason == SH_PUMP_REASON_DIFF || in->reason == SH_PUMP_REASON_MIN_RUN ||
           in->reason == SH_PUMP_REASON_COL_PROTECT;
}

static void e6_step(sh_alarm_state_t *st, const sh_alarm_params_t *p, const sh_alarm_in_t *in,
                    uint32_t now_s)
{
    if (!normal_run(in)) {
        st->run = false;            /* no check; E6 stays as it is */
        return;
    }
    uint32_t win_min = p->e6_time_s / SAMPLE_S;
    if (win_min < 1) {
        win_min = 1;
    }
    if (win_min > SH_ALARM_E6_MAX_MIN) {
        win_min = SH_ALARM_E6_MAX_MIN;
    }
    if (!st->run) {
        /* a new run: the first `e6_time_s` are skipped, the window starts after them */
        st->run = true;
        st->n = 0;
        st->head = 0;
        st->next_sample_s = now_s + win_min * SAMPLE_S;
    }
    if (now_s < st->next_sample_s) {
        return;
    }
    st->next_sample_s += SAMPLE_S;

    const uint8_t size = (uint8_t)(win_min + 1);   /* samples spanning the window */
    st->col[st->head] = in->t_col;
    st->tank[st->head] = in->t_tank;
    st->head = (uint8_t)((st->head + 1) % size);
    if (st->n < size) {
        st->n++;
    }
    if (st->n < size) {
        return;                     /* the window is not full yet */
    }
    /* full ring: head is the oldest sample (one window ago), the newest is now */
    const int32_t col_rise = (int32_t)in->t_col - st->col[st->head];
    const int32_t tank_rise = (int32_t)in->t_tank - st->tank[st->head];
    const int32_t diff = (int32_t)in->t_col - in->t_tank;
    if (!st->e6) {
        st->e6 = col_rise >= p->e6_col_rise && tank_rise < p->e6_tank_rise && diff >= p->e6_diff;
    } else if (tank_rise >= p->e6_tank_rise || diff < p->e6_clear_diff) {
        st->e6 = false;
    }
}

uint8_t sh_alarm_step(sh_alarm_state_t *st, const sh_alarm_params_t *p, const sh_alarm_in_t *in,
                      uint32_t now_s)
{
    e6_step(st, p, in, now_s);

    uint8_t a = 0;
    if (in->col_fault) {
        a |= SH_ALARM_BIT(SH_ALARM_E1);
    }
    if (in->tank_fault) {
        a |= SH_ALARM_BIT(SH_ALARM_E2);
    }
    if (in->tank_ceiling && !in->tank_fault) {
        a |= SH_ALARM_BIT(SH_ALARM_E4);
    }
    if (in->col_boiling && !in->col_fault) {
        a |= SH_ALARM_BIT(SH_ALARM_E5);
    }
    if (st->e6) {
        a |= SH_ALARM_BIT(SH_ALARM_E6);
    }
    if (in->pump_on &&
        (in->reason == SH_PUMP_REASON_COL_PROTECT || in->reason == SH_PUMP_REASON_TF_PROBE)) {
        a |= SH_ALARM_BIT(SH_ALARM_HOT);
    }
    if (in->pump_on && in->reason == SH_PUMP_REASON_FROST) {
        a |= SH_ALARM_BIT(SH_ALARM_FRO);
    }
    st->active = a;
    return a;
}

sh_alarm_t sh_alarm_code_at(uint8_t active, uint32_t ms)
{
    uint32_t idx = ms / SH_ALARM_CODE_MS;
    for (int a = 0; a < SH_ALARM_COUNT; a++) {
        if (active & SH_ALARM_BIT(a)) {
            if (idx == 0) {
                return (sh_alarm_t)a;
            }
            idx--;
        }
    }
    return SH_ALARM_COUNT;
}

const char *sh_alarm_name(sh_alarm_t a)
{
    static const char *const names[SH_ALARM_COUNT] = {
        [SH_ALARM_E1] = "E1", [SH_ALARM_E2] = "E2", [SH_ALARM_E4] = "E4",
        [SH_ALARM_E5] = "E5", [SH_ALARM_E6] = "E6", [SH_ALARM_HOT] = "Hot", [SH_ALARM_FRO] = "Fro",
    };
    return (unsigned)a < SH_ALARM_COUNT ? names[a] : "?";
}
