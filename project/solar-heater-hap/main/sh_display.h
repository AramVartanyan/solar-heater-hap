/* TM1637, 3 digits (doc/hardware.md), library esp_tm1637 1.1.0.
 * Phase 1: boot test, then the collector temperature ("---" while the sensor is missing). */
#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t sh_display_init(void);
void sh_display_temp(int16_t t_tenths);     /* SH_TEMP_INVALID -> "---"; updates only on change */
