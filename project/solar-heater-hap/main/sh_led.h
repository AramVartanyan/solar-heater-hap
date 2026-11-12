/* Onboard WS2812 status LED (doc/led-indication.md): shows a colour with a pattern. What to show
 * is decided by sh_led_logic (plain C); the patterns are animated here with one esp_timer. */
#pragma once

#include "esp_err.h"
#include "sh_led_logic.h"

esp_err_t sh_led_init(void);

/* Shows the colour with the pattern; nothing happens when both are already shown (a pattern is
 * not restarted). The LED itself is written only when its light changes. */
void sh_led_show(sh_color_t color, sh_led_pattern_t pattern);
