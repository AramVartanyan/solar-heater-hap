/* Onboard WS2812 status LED (doc/led-indication.md). Phase 1: plain colours;
 * patterns (blink, breathe, heartbeat) follow with the indication logic. */
#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    SH_COLOR_OFF = 0,
    SH_COLOR_BLUE,      /* idle */
    SH_COLOR_GREEN,     /* pump running */
    SH_COLOR_WHITE,     /* cold collector / OTA */
    SH_COLOR_YELLOW,    /* hot collector */
    SH_COLOR_PURPLE,    /* vacation */
    SH_COLOR_RED,       /* alarm (blinking only) */
} sh_color_t;

esp_err_t sh_led_init(void);
void sh_led_set(sh_color_t color);  /* updates the LED only when the colour changes */
