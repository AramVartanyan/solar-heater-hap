/* TM1637, 3 digits (doc/hardware.md), library esp_tm1637 1.1.1. The display is written by the
 * display task only; it also owns the user menu, so the keys come to it through a queue.
 *   normal screen: "Boil" 1 s -> tank temperature 5 s -> "Col" 1 s -> collector temperature 5 s
 *                  ("---" for a faulty sensor or one without a value yet)
 *   pump test:     "tSt" 3 s, then the seconds left; "tSt" again for 1 s every 7 s
 *   menu:          names, blinking value, confirmed value (sh_menu) */
#pragma once

#include "esp_err.h"
#include "sh_menu.h"

esp_err_t sh_display_init(void);    /* boot test, then starts the display task */

/* A key from the buttons; does not block (safe from any task). */
void sh_display_key(sh_key_t key);
