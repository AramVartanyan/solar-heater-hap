/* The learned values of group C in the "nvs" partition (namespace "sh_learned", blob "v"): the
 * sunrises and sunsets of the last 7 days and the correction of the night cooling target
 * (sh_modes, sh_cool). Read at boot, written when they change (at most a few times a day), erased by
 * the reset of the learned values and by the factory reset. */
#pragma once

#include "esp_err.h"
#include "sh_modes.h"

esp_err_t sh_learned_load(sh_learned_t *out);           /* after nvs_flash_init(); nothing stored: none */
esp_err_t sh_learned_save(const sh_learned_t *l);
esp_err_t sh_learned_erase(void);
