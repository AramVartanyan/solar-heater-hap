/* DS18B20 collector / tank, each on its own 1-Wire GPIO (doc/hardware.md).
 * Phase 1: stub — the driver (espressif/onewire_bus + ds18b20) comes next. */
#pragma once

#include "esp_err.h"

esp_err_t sh_sensors_init(void);
void sh_sensors_read(void);         /* publishes to sh_state (SH_TEMP_INVALID on error) */
