/* TM1637, 3 digits (doc/hardware.md), library esp_tm1637 1.1.1.
 * Normal mode: the display task alternates "label 1 s -> temperature 5 s" for the tank and the
 * collector ("---" while a sensor is missing). The display is used by this task only. */
#pragma once

#include "esp_err.h"

esp_err_t sh_display_init(void);    /* boot test, then starts the display task */
