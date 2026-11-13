/* Created by Aram Vartanyan (c) 2026 */

/* DS18B20 collector / tank, each on its own 1-Wire GPIO (doc/hardware.md), library ds18b20.
 * Both sensors convert at the same time. A failed reading (no answer, CRC, out of range, jump
 * > 20 K) is repeated at once with a new conversion; a failed measurement keeps the last valid
 * value, 5 in a row make the sensor faulty (sh_meas). The measurement runs in its own task every
 * CONFIG_SH_MEASURE_PERIOD_S (no averaging) and publishes to sh_state (last valid values, faults);
 * the control loop only reads the last published values. */
#pragma once

#include "esp_err.h"

esp_err_t sh_sensors_init(void);
esp_err_t sh_sensors_start(void);   /* first measurement (~0.8 s), then the measurement task */
