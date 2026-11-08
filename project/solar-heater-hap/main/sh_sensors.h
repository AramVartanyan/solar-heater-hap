/* DS18B20 collector / tank, each on its own 1-Wire GPIO (doc/hardware.md), library ds18b20.
 * Both sensors convert at the same time; a missing sensor or a read error gives SH_TEMP_INVALID. */
#pragma once

#include "esp_err.h"

esp_err_t sh_sensors_init(void);
void sh_sensors_read(void);         /* blocks up to ~0.8 s; publishes to sh_state */
