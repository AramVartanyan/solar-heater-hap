/* Learned values in the "nvs" partition (namespace "sh_learn"): the sunrises and sunsets of the last
 * days (sh_sun) and the correction of the night cooling target with its count of quiet mornings
 * (sh_cool). Read at boot by the control task, written when they change (a few times a day at
 * most). The reset of the learned values (web interface) erases them. */
#pragma once

#include "esp_err.h"
#include "sh_sun.h"

typedef struct {
    sh_sun_days_t days;
    int8_t cool_offset;         /* K */
    uint8_t cool_quiet;
} sh_learned_t;

void sh_learn_load(sh_learned_t *out);          /* nothing stored / invalid: empty, 0, 0 */
esp_err_t sh_learn_save(const sh_learned_t *l);
esp_err_t sh_learn_reset(void);
