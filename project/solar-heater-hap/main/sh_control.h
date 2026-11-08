/* Control loop: reads sensors, decides the pump, updates the indication.
 * Phase 1: periodic task that only reads (stub) sensors and keeps the pump off. */
#pragma once

#include "esp_err.h"

esp_err_t sh_control_start(void);
