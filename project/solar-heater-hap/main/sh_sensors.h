/* DS18B20 collector / tank, each on its own 1-Wire GPIO (doc/hardware.md), library ds18b20.
 * Both sensors convert at the same time; a missing sensor or a read error gives SH_TEMP_INVALID.
 * The measurement runs in its own task every CONFIG_SH_MEASURE_PERIOD_S (no averaging) and
 * publishes to sh_state; the control loop only reads the last published values. */
#pragma once

#include "esp_err.h"

esp_err_t sh_sensors_init(void);
esp_err_t sh_sensors_start(void);   /* first measurement (~0.8 s), then the measurement task */
