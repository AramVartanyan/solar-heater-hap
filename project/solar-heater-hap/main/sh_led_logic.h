/* Status LED: state -> (colour, pattern), and the times of the patterns (doc/led-indication.md;
 * doc/tasks/step5-alarms-indication.md 3.3) — plain C, no ESP-IDF / FreeRTOS, compiled into the host
 * test as well. sh_led drives the WS2812 by it.
 *
 * Colour, the first match wins:
 *   red      an alarm (E1, E2, E4, E5, E6)
 *   orange   the pump runs by a protection (Hot)
 *   purple   vacation
 *   yellow   hot collector (above 90 °C, until below 88 °C), pump stopped
 *   green    the pump runs
 *   white    cold collector (below 20 °C, until above 22 °C)
 *   blue     idle
 * Pattern:
 *   red      only blinking: fast 150 / 150 ms with E4 or E5 (also together with the others);
 *            double (2 x 100 ms, 100 ms apart, then 1 s dark) with E1, E2, E6
 *   others   steady with Wi-Fi; a flash of 80 ms every 2 s without it or while the Wi-Fi setup
 *            portal is open
 * Temperatures are tenths of °C. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SH_COLOR_OFF = 0,
    SH_COLOR_BLUE,      /* idle */
    SH_COLOR_GREEN,     /* pump running */
    SH_COLOR_WHITE,     /* cold collector / OTA */
    SH_COLOR_YELLOW,    /* hot collector */
    SH_COLOR_ORANGE,    /* pump running by a protection */
    SH_COLOR_PURPLE,    /* vacation */
    SH_COLOR_RED,       /* alarm (blinking only) */
    SH_COLOR_COUNT
} sh_color_t;

typedef enum {
    SH_LED_STEADY = 0,  /* online */
    SH_LED_FLASH,       /* no Wi-Fi: 80 ms every 2 s */
    SH_LED_FAST,        /* overheating: 150 ms on / 150 ms off */
    SH_LED_DOUBLE,      /* failure: 100 ms on, 100 ms off, 100 ms on, 1 s off */
} sh_led_pattern_t;

/* Pattern times, ms */
#define SH_LED_FLASH_ON_MS      80
#define SH_LED_FLASH_PERIOD_MS  2000
#define SH_LED_FAST_ON_MS       150
#define SH_LED_FAST_OFF_MS      150
#define SH_LED_DOUBLE_ON_MS     100
#define SH_LED_DOUBLE_GAP_MS    100
#define SH_LED_DOUBLE_DARK_MS   1000

/* Collector thresholds of the colours (Kconfig "Device Settings") */
typedef struct {
    int16_t cold;               /* white below this, until above this + 2 K */
    int16_t hot;                /* yellow above this, until below this - 2 K */
} sh_led_params_t;

/* The inputs; the function is called when one of them changes. */
typedef struct {
    uint8_t alarms;             /* active codes: SH_ALARM_BIT() of sh_alarms.h */
    bool pump_on;
    bool vacation;
    int16_t t_col;              /* collector; SH_TEMP_INVALID: no value, or the sensor is faulty */
    bool wifi_connected;
    bool wifi_portal;           /* Wi-Fi setup portal open */
} sh_led_in_t;

/* Memory of the return zones; all zeros at boot. */
typedef struct {
    bool cold;
    bool hot;
} sh_led_mem_t;

typedef struct {
    sh_color_t color;
    sh_led_pattern_t pattern;
} sh_led_out_t;

/* state -> (colour, pattern). Updates the memory of the return zones. */
void sh_led_decide(const sh_led_params_t *p, sh_led_mem_t *m, const sh_led_in_t *in, sh_led_out_t *out);

/* Length of one period of the pattern, ms; 0 = steady. */
uint32_t sh_led_pattern_period(sh_led_pattern_t pattern);

/* Is the LED lit `ms` after the start of a period (ms < period), and in how many ms it changes next
 * (`*next_ms`; 0 = never). */
bool sh_led_pattern_lit(sh_led_pattern_t pattern, uint32_t ms, uint32_t *next_ms);
