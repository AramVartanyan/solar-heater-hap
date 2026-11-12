/* Status LED: state -> (colour, pattern) — see sh_led_logic.h. */

#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarms.h"
#include "sh_led_logic.h"

void sh_led_decide(const sh_led_params_t *p, sh_led_mem_t *m, const sh_led_in_t *in, sh_led_out_t *out)
{
    /* Strictly below / above the threshold, back strictly beyond the 2 K return zone. */
    if (in->t_col != SH_TEMP_INVALID) {
        if (in->t_col < p->cold) {
            m->cold = true;
        } else if (in->t_col > p->cold + SH_PUMP_RETURN_ZONE) {
            m->cold = false;
        }
        if (in->t_col > p->hot) {
            m->hot = true;
        } else if (in->t_col < p->hot - SH_PUMP_RETURN_ZONE) {
            m->hot = false;
        }
    }

    if (in->alarms & SH_ALARMS_ERROR) {
        /* red only blinks; the alarm patterns replace the pattern of the connection */
        out->color = SH_COLOR_RED;
        out->pattern = (in->alarms & SH_ALARMS_OVERHEAT) ? SH_LED_FAST : SH_LED_DOUBLE;
        return;
    }

    if (in->alarms & SH_ALARM_BIT(SH_ALARM_HOT)) {
        out->color = SH_COLOR_ORANGE;
    } else if (in->vacation) {
        out->color = SH_COLOR_PURPLE;
    } else if (m->hot && !in->pump_on) {
        out->color = SH_COLOR_YELLOW;
    } else if (in->pump_on) {
        out->color = SH_COLOR_GREEN;
    } else if (m->cold) {
        out->color = SH_COLOR_WHITE;
    } else {
        out->color = SH_COLOR_BLUE;
    }
    out->pattern = (in->wifi_connected && !in->wifi_portal) ? SH_LED_STEADY : SH_LED_FLASH;
}

uint32_t sh_led_pattern_period(sh_led_pattern_t pattern)
{
    switch (pattern) {
    case SH_LED_FLASH:
        return SH_LED_FLASH_PERIOD_MS;
    case SH_LED_FAST:
        return SH_LED_FAST_ON_MS + SH_LED_FAST_OFF_MS;
    case SH_LED_DOUBLE:
        return 2 * SH_LED_DOUBLE_ON_MS + SH_LED_DOUBLE_GAP_MS + SH_LED_DOUBLE_DARK_MS;
    default:
        return 0;
    }
}

bool sh_led_pattern_lit(sh_led_pattern_t pattern, uint32_t ms, uint32_t *next_ms)
{
    const uint32_t period = sh_led_pattern_period(pattern);
    if (period == 0) {
        *next_ms = 0;
        return true;
    }
    /* the ends of the lit / dark parts of one period, lit first */
    uint32_t edge[4];
    int edges;
    switch (pattern) {
    case SH_LED_FLASH:
        edge[0] = SH_LED_FLASH_ON_MS;
        edges = 1;
        break;
    case SH_LED_FAST:
        edge[0] = SH_LED_FAST_ON_MS;
        edges = 1;
        break;
    default:    /* SH_LED_DOUBLE */
        edge[0] = SH_LED_DOUBLE_ON_MS;
        edge[1] = edge[0] + SH_LED_DOUBLE_GAP_MS;
        edge[2] = edge[1] + SH_LED_DOUBLE_ON_MS;
        edges = 3;
        break;
    }
    edge[edges] = period;
    ms %= period;
    for (int i = 0; i <= edges; i++) {
        if (ms < edge[i]) {
            *next_ms = edge[i] - ms;
            return i % 2 == 0;
        }
    }
    *next_ms = 0;   /* not reached: ms < period */
    return true;
}
