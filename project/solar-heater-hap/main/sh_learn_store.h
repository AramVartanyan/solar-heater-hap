/* Learned values in the "nvs" partition (namespace "sh_learn"): the sunrises and sunsets of the last
 * days (sh_sun), the correction of the night cooling target with its count of quiet mornings
 * (sh_cool), the heater found with the setting "0 kW" (sh_heater), and E8 with its counters of
 * nights and the normal cooling of the last normal nights (sh_e8). Read at boot by the control task,
 * written when they change (a few times a day at most). The reset of the learned values (web
 * interface) erases them. */
#pragma once

#include "esp_err.h"
#include "sh_sun.h"
#include "sh_e8.h"

typedef struct {
    sh_sun_days_t days;
    int8_t cool_offset;         /* K */
    uint8_t cool_quiet;
    bool heater_found;
    sh_e8_keep_t e8;
} sh_learned_t;

void sh_learn_load(sh_learned_t *out);          /* nothing stored / invalid: empty, 0, 0, not found, no E8 */
esp_err_t sh_learn_save(const sh_learned_t *l);
esp_err_t sh_learn_reset(void);
