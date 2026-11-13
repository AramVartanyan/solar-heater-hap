/* Onboard WS2812 status LED (doc/led-indication.md): a colour and a pattern from sh_ind. The
 * patterns run on one periodic esp_timer (10 ms); the LED is written only when what it shows
 * changes. Brightness: CONFIG_SH_LED_BRIGHTNESS; the breathing (HomeKit pairing window) goes between
 * 0 and it. */
#pragma once

#include "esp_err.h"
#include "sh_ind.h"

esp_err_t sh_led_init(void);
/* Shows `color` with `pattern`; a new pattern starts from its beginning, a new colour keeps the
 * running pattern. */
void sh_led_show(sh_color_t color, sh_ind_pattern_t pattern);
/* HomeKit Identify: 3 groups of 2 flashes in the shown colour, once, over the running pattern (not
 * over the firmware update). Any task. */
void sh_led_identify(void);
