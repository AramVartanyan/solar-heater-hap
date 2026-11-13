/* Created by Aram Vartanyan (c) 2026 */

/* Status LED indication — see sh_ind.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarm.h"
#include "sh_ind.h"

sh_ind_out_t sh_ind_update(sh_ind_state_t *st, const sh_ind_params_t *p, const sh_ind_in_t *in)
{
    /* white / yellow with the 2 K return zone; without a usable collector value neither */
    if (in->col_fault || in->t_col == SH_TEMP_INVALID) {
        st->cold = false;
        st->hot = false;
    } else {
        if (in->t_col < p->cold) {
            st->cold = true;
        } else if (in->t_col > p->cold + SH_IND_RETURN_ZONE) {
            st->cold = false;
        }
        if (in->t_col > p->hot) {
            st->hot = true;
        } else if (in->t_col < p->hot - SH_IND_RETURN_ZONE) {
            st->hot = false;
        }
    }

    sh_ind_out_t out;
    if (in->ota) {
        out.color = SH_COLOR_WHITE;
        out.pattern = SH_IND_BLINK;
        return out;
    }
    if (in->alarms & SH_ALARM_OVERHEAT) {
        out.color = SH_COLOR_RED;
        out.pattern = SH_IND_FAST;
        return out;
    }
    if (in->alarms & SH_ALARM_REPAIR) {
        out.color = SH_COLOR_RED;
        out.pattern = SH_IND_DOUBLE;
        return out;
    }
    if ((in->alarms & SH_ALARM_PROTECT) || (in->cooling && in->pump_on)) {
        out.color = SH_COLOR_ORANGE;
    } else if (in->vacation) {
        out.color = SH_COLOR_PURPLE;
    } else if (st->hot && !in->pump_on) {
        out.color = SH_COLOR_YELLOW;
    } else if (in->pump_on) {
        out.color = SH_COLOR_GREEN;
    } else if (st->cold) {
        out.color = SH_COLOR_WHITE;
    } else {
        out.color = SH_COLOR_BLUE;
    }
    if (in->ap) {
        out.pattern = SH_IND_WINK;
    } else if (!in->wifi_connected || in->wifi_portal) {
        out.pattern = SH_IND_HEARTBEAT;
    } else {
        out.pattern = in->pair_window ? SH_IND_BREATHE : SH_IND_STEADY;
    }
    return out;
}

bool sh_ind_lit(sh_ind_pattern_t pattern, uint32_t ms)
{
    switch (pattern) {
    case SH_IND_BREATHE:
        return sh_ind_level(pattern, ms) > 0;
    case SH_IND_IDENTIFY: {
        if (ms >= SH_IND_IDENT_MS) {
            return true;                /* over: what was shown before */
        }
        const uint32_t t = ms % SH_IND_IDENT_GROUP_MS;
        /* each flash: dark, then lit; the gap between the groups is lit */
        return t >= SH_IND_IDENT_FLASHES * 2u * SH_IND_IDENT_FLASH_MS || (t / SH_IND_IDENT_FLASH_MS) % 2u == 1u;
    }
    case SH_IND_BLINK:
        return ms % (2 * SH_IND_BLINK_MS) < SH_IND_BLINK_MS;
    case SH_IND_WINK:
        return ms % SH_IND_WINK_PERIOD_MS >= SH_IND_WINK_OFF_MS;
    case SH_IND_FAST:
        return ms % (SH_IND_FAST_ON_MS + SH_IND_FAST_OFF_MS) < SH_IND_FAST_ON_MS;
    case SH_IND_DOUBLE: {
        const uint32_t t = ms % (2 * SH_IND_DOUBLE_ON_MS + SH_IND_DOUBLE_GAP_MS + SH_IND_DOUBLE_DARK_MS);
        return t < SH_IND_DOUBLE_ON_MS ||
               (t >= SH_IND_DOUBLE_ON_MS + SH_IND_DOUBLE_GAP_MS &&
                t < 2 * SH_IND_DOUBLE_ON_MS + SH_IND_DOUBLE_GAP_MS);
    }
    case SH_IND_HEARTBEAT:
        return ms % SH_IND_BEAT_PERIOD_MS < SH_IND_BEAT_ON_MS;
    case SH_IND_STEADY:
    default:
        return true;
    }
}

uint8_t sh_ind_level(sh_ind_pattern_t pattern, uint32_t ms)
{
    if (pattern != SH_IND_BREATHE) {
        return sh_ind_lit(pattern, ms) ? 100 : 0;
    }
    const uint32_t t = ms % (2 * SH_IND_BREATHE_HALF_MS + SH_IND_BREATHE_DARK_MS);
    if (t >= 2 * SH_IND_BREATHE_HALF_MS) {
        return 0;                       /* the dark bottom of a breath */
    }
    const uint32_t pos = t < SH_IND_BREATHE_HALF_MS ? t : 2 * SH_IND_BREATHE_HALF_MS - t;
    return (uint8_t)(100u * pos * pos / (SH_IND_BREATHE_HALF_MS * SH_IND_BREATHE_HALF_MS));     /* squared: smooth to the eye */
}

bool sh_ind_identify_ok(sh_ind_pattern_t pattern)
{
    return pattern != SH_IND_BLINK;
}
