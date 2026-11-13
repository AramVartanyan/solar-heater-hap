/* Pulse mode: when the next pulse is due — see sh_pulse.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_pulse.h"

uint32_t sh_pulse_day_s(int16_t col)
{
    if (col == SH_TEMP_INVALID || col < SH_PULSE_WARM) {
        return SH_PULSE_COLD_S;
    }
    return col <= SH_PULSE_HOT ? SH_PULSE_WARM_S : SH_PULSE_HOT_S;
}

uint32_t sh_pulse_frost_s(int16_t margin)
{
    if (margin <= SH_PULSE_FROST_NEAR) {
        return SH_PULSE_FROST_NEAR_S;
    }
    return margin <= SH_PULSE_FROST_MID ? SH_PULSE_FROST_MID_S : SH_PULSE_FROST_FAR_S;
}

bool sh_pulse_window_at(int rise, int set, uint16_t min)
{
    if (rise >= 0 && set >= 0 && rise < set) {
        return min >= rise && min < set;
    }
    return min >= SH_PULSE_WIN_FROM_MIN && min < SH_PULSE_WIN_TO_MIN;
}

/* The pulses start again after an overheated collector. */
static bool hot_over(const sh_pulse_t *p, const sh_pulse_in_t *in, uint32_t now_s)
{
    const bool waited = now_s - p->hot_s >= SH_PULSE_HOT_WAIT_S;
    if (!in->reliable || !in->lt.set) {
        return waited;
    }
    const bool next_day = in->lt.day != p->hot_day;
    if (in->set >= 0) {
        return next_day || in->lt.min >= in->set;                       /* the learned sunset */
    }
    return (next_day || in->lt.min >= SH_PULSE_WIN_TO_MIN) && waited;   /* 18:00, not earlier than 5 h */
}

void sh_pulse_step(sh_pulse_t *p, const sh_pulse_in_t *in, uint32_t now_s)
{
    p->events = 0;
    p->window = in->reliable && in->lt.set ? sh_pulse_window_at(in->rise, in->set, in->lt.min) : in->risen;

    /* the collector at the boiling point - 10 K: no pulses until it is safe again */
    if (in->fresh && in->rel_col != SH_TEMP_INVALID && in->rel_col >= in->boiling - SH_PULSE_BOIL_MARGIN) {
        if (!p->hot) {
            p->events |= SH_PULSE_EVT_HOT;
        }
        p->hot = true;
        p->hot_s = now_s;
        p->hot_day = in->lt.set ? in->lt.day : 0;
    } else if (p->hot && hot_over(p, in, now_s)) {
        p->hot = false;
        p->events |= SH_PULSE_EVT_HOT_END;
    }

    /* frost: the gate (threshold + 5 K) only starts the watch — the collector pipe with the pump
     * stopped below it (a pulse at once) or a reliable value below it (that pulse counts); the
     * watch lasts until the window of the pulses opens; the next night starts at the gate again */
    if (!in->frost_on || p->window) {
        p->watch = false;
    } else if (!p->watch) {
        const int16_t gate = (int16_t)(in->frost + SH_PULSE_GATE);
        const bool by_pipe = !in->pump_on && in->pipe_col != SH_TEMP_INVALID && in->pipe_col < gate;
        const bool by_value = in->fresh && in->rel_col != SH_TEMP_INVALID && in->rel_col < gate;
        if (by_pipe || by_value) {
            p->watch = true;
            p->watch_s = now_s;
        }
    }

    p->due = false;
    if (p->hot) {
        p->next_s = SH_PULSE_NONE;
        return;
    }
    if (!in->rel) {
        p->next_s = now_s;
        p->due = !in->pump_on;
        return;
    }
    bool now = false;
    uint32_t wait;
    if (p->window) {
        wait = sh_pulse_day_s(in->rel_col);
    } else if (p->watch && in->rel_col != SH_TEMP_INVALID) {
        /* the frost watch replaces the night pulse of 3 h; the cooling may ask for more */
        wait = sh_pulse_frost_s((int16_t)(in->rel_col - in->frost));
        if (in->cool && SH_PULSE_COOL_S < wait) {
            wait = SH_PULSE_COOL_S;
        }
        now = in->rel_s < p->watch_s ||                                 /* the watch just started */
              (!in->pump_on && in->pipe_col != SH_TEMP_INVALID &&
               in->pipe_col <= in->rel_col - SH_PULSE_FROST_FALL);      /* the pipe fell 3 K */
    } else {
        wait = in->cool ? SH_PULSE_COOL_S : SH_PULSE_NIGHT_S;
    }
    p->next_s = now ? now_s : in->rel_s + wait;
    p->due = !in->pump_on && (now || now_s - in->rel_s >= wait);
}
