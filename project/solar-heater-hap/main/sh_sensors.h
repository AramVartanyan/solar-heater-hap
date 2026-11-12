/* DS18B20 collector / tank, each on its own 1-Wire GPIO (doc/hardware.md), library ds18b20.
 * Both sensors convert at the same time. A reading that fails (no answer, checksum, value out of
 * range, jump of more than 20 K) is repeated once with a new conversion; a failed measurement keeps
 * the last valid value, 5 in a row declare the sensor faulty (sh_sensor_logic).
 * The measurement runs in its own task every CONFIG_SH_MEASURE_PERIOD_S (no averaging) and
 * publishes to sh_state the last valid values and the faults; the control loop only reads them. */
#pragma once

#include "esp_err.h"

esp_err_t sh_sensors_init(void);
esp_err_t sh_sensors_start(void);   /* first measurement (~0.8 s), then the measurement task */
