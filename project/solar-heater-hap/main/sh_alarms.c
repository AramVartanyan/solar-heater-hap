/* Alarms and information — see sh_alarms.h. */

#include <stddef.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarms.h"

#define E6_SAMPLES      (SH_E6_SLOTS + 1)       /* both ends of the window */

/* The pump runs by the normal control: the only runs E6 is judged in. */
static bool normal_run(const sh_alarm_in_t *in)
{
    if (!in->pump_on || in->col_fault || in->tank_fault || in->t_col == SH_TEMP_INVALID ||
        in->t_tank == SH_TEMP_INVALID) {
        return false;
    }
    return in->pump_reason == SH_PUMP_REASON_DIFF || in->pump_reason == SH_PUMP_REASON_MIN_RUN ||
           in->pump_reason == SH_PUMP_REASON_COL_PROTECT;
}

static void e6_step(sh_alarms_t *a, const sh_alarm_params_t *p, const sh_alarm_in_t *in, uint32_t now_s)
{
    if (!normal_run(in)) {
        a->run = false;             /* the next run starts its own samples */
        return;
    }
    if (!a->run) {
        a->run = true;
        a->n = 0;
        a->head = 0;
        a->next_s = now_s;          /* the first sample: the start of the run */
    }
    if (now_s < a->next_s) {
        return;
    }
    const uint32_t period_s = p->window_s / SH_E6_SLOTS;
    a->next_s += period_s ? period_s : 1;
    a->col[a->head] = in->t_col;
    a->tank[a->head] = in->t_tank;
    a->head = (uint8_t)((a->head + 1) % E6_SAMPLES);
    if (a->n < E6_SAMPLES) {
        a->n++;
    }
    if (a->n < E6_SAMPLES) {
        return;                     /* the run is not one window long yet */
    }

    /* full ring: `head` is the sample one window ago */
    const int32_t col_rise = (int32_t)in->t_col - a->col[a->head];
    const int32_t tank_rise = (int32_t)in->t_tank - a->tank[a->head];
    const int32_t diff = (int32_t)in->t_col - in->t_tank;
    if (tank_rise >= p->tank_rise || diff < p->clear_diff) {
        a->e6 = false;              /* there is flow */
    } else if (col_rise >= p->col_rise && diff >= p->diff) {
        a->e6 = true;
    }
}

uint8_t sh_alarms_step(sh_alarms_t *a, const sh_alarm_params_t *p, const sh_alarm_in_t *in,
                       uint32_t now_s)
{
    /* The limits follow every valid temperature of a sensor that is not faulty (a faulty sensor's
     * last valid value is not used), as in sh_pump_ctl. */
    const bool col_ok = !in->col_fault && in->t_col != SH_TEMP_INVALID;
    const bool tank_ok = !in->tank_fault && in->t_tank != SH_TEMP_INVALID;
    if (col_ok) {
        a->col_boiling = sh_pump_latch(a->col_boiling, in->t_col, SH_PUMP_COL_BOILING);
    }
    if (tank_ok) {
        a->tank_ceiling = sh_pump_latch(a->tank_ceiling, in->t_tank, SH_PUMP_TANK_CEILING);
    }
    e6_step(a, p, in, now_s);

    uint8_t active = 0;
    if (in->col_fault) {
        active |= SH_ALARM_BIT(SH_ALARM_E1);
    }
    if (in->tank_fault) {
        active |= SH_ALARM_BIT(SH_ALARM_E2);
    }
    if (a->tank_ceiling && !in->tank_fault) {
        active |= SH_ALARM_BIT(SH_ALARM_E4);
    }
    if (a->col_boiling && !in->col_fault) {
        active |= SH_ALARM_BIT(SH_ALARM_E5);
    }
    if (a->e6) {
        active |= SH_ALARM_BIT(SH_ALARM_E6);
    }
    if (in->pump_on && (in->pump_reason == SH_PUMP_REASON_COL_PROTECT ||
                        in->pump_reason == SH_PUMP_REASON_TF_PROBE)) {
        active |= SH_ALARM_BIT(SH_ALARM_HOT);
    }
    a->active = active;
    return active;
}

static const char *const s_text[SH_ALARM_COUNT] = {
    [SH_ALARM_E1]  = " E1",
    [SH_ALARM_E2]  = " E2",
    [SH_ALARM_E4]  = " E4",
    [SH_ALARM_E5]  = " E5",
    [SH_ALARM_E6]  = " E6",
    [SH_ALARM_HOT] = "Hot",
};

const char *sh_alarm_text(sh_alarm_t code)
{
    return (unsigned)code < SH_ALARM_COUNT ? s_text[code] : " E0";
}

const char *sh_alarm_name(sh_alarm_t code)
{
    const char *text = sh_alarm_text(code);
    return text[0] == ' ' ? text + 1 : text;
}

const char *sh_alarm_desc(sh_alarm_t code)
{
    static const char *const desc[SH_ALARM_COUNT] = {
        [SH_ALARM_E1]  = "collector sensor faulty",
        [SH_ALARM_E2]  = "tank sensor faulty",
        [SH_ALARM_E4]  = "tank at the emergency ceiling",
        [SH_ALARM_E5]  = "no pump start, collector at the boiling point",
        [SH_ALARM_E6]  = "pump failure / no flow",
        [SH_ALARM_HOT] = "the pump runs by the collector protection",
    };
    return (unsigned)code < SH_ALARM_COUNT ? desc[code] : "?";
}

sh_screen_t sh_alarm_screen(uint8_t active, uint32_t ms, sh_alarm_t *code)
{
    if (ms < SH_SCREEN_LABEL_MS) {
        return SH_SCREEN_BOIL;
    }
    ms -= SH_SCREEN_LABEL_MS;
    if (ms < SH_SCREEN_VALUE_MS) {
        return SH_SCREEN_T_TANK;
    }
    ms -= SH_SCREEN_VALUE_MS;
    if (ms < SH_SCREEN_LABEL_MS) {
        return SH_SCREEN_COL;
    }
    ms -= SH_SCREEN_LABEL_MS;
    if (ms < SH_SCREEN_VALUE_MS) {
        return SH_SCREEN_T_COL;
    }
    ms -= SH_SCREEN_VALUE_MS;
    /* the enum is in the order of the display: the alarms by number, then the information */
    for (int c = 0; c < SH_ALARM_COUNT; c++) {
        if (!(active & SH_ALARM_BIT(c))) {
            continue;
        }
        if (ms < SH_SCREEN_CODE_MS) {
            *code = (sh_alarm_t)c;
            return SH_SCREEN_CODE;
        }
        ms -= SH_SCREEN_CODE_MS;
    }
    return SH_SCREEN_END;
}
