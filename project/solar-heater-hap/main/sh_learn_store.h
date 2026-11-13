/* Created by Aram Vartanyan (c) 2026 */

/* Learned values in the "nvs" partition (namespace "sh_learn"): the sunrises and sunsets of the last
 * days (sh_sun), the correction of the night cooling target with its count of quiet mornings
 * (sh_cool), the heater detected with "0 kW" (sh_heater) and the normal night cooling of the tank
 * with the E8 counters (sh_e8). Read at boot by the control task, written when they change (a few
 * times a day at most). The reset of the learned values (web interface) erases them, except the E8
 * counters and E8 itself (an alarm is not cleared by forgetting what was learned). */
#pragma once

#include "esp_err.h"
#include "sh_sun.h"
#include "sh_e8.h"

typedef struct {
    sh_sun_days_t days;
    int8_t cool_offset;         /* K */
    uint8_t cool_quiet;
    bool heater;                /* detected with "0 kW" */
    sh_e8_learned_t e8;
} sh_learned_t;

void sh_learn_load(sh_learned_t *out);          /* nothing stored / invalid: empty, 0, no heater, no E8 */
esp_err_t sh_learn_save(const sh_learned_t *l);
esp_err_t sh_learn_reset(void);
