/* TM1637, 3 digits (doc/hardware.md), library esp_tm1637 1.1.1. The display task is the only one
 * that writes to the display; it shows, in this order of precedence:
 *   - the user menu (sh_buttons / sh_menu): item names, the value (blinking while edited);
 *   - the pump test: "tSt" for 3 s, then the seconds left, "tSt" again for 1 s every 7 s;
 *   - the normal screen: "label 1 s -> temperature 5 s" for the tank and the collector ("---" for a
 *     faulty sensor or before the first valid value); it starts again from "Boil" on every return. */
#pragma once

#include "esp_err.h"

esp_err_t sh_display_init(void);    /* boot test, then starts the display task */
