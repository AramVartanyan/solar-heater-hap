/* The units of the display and of the web interface (doc/operating-modes.md 3, Temperature Display
 * Units; doc/tasks/step14-homekit.md 3.1) — plain C, compiled into the host test too.
 *
 * Everything inside stays in °C: the measurement, the logic, the settings in NVS (whole °C / K),
 * the history, HomeKit (its values are always °C). The units only change what a person sees: with
 * °F (chosen in Apple Home, kept in NVS, °C without a choice) the display shows the temperatures and
 * the menu values in °F — a difference (d.on, d.oF) x 1.8 without +32. Whole °C -> whole °F,
 * tenths of °C -> tenths of °F (rounded half away from zero). The step of a setting stays 1 °C /
 * 1 K, so in °F the values move unevenly (tbo 86, 88, 90, 91, ...). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SH_UNITS_C = 0,             /* HAP Temperature Display Units: 0 Celsius */
    SH_UNITS_F = 1,             /* 1 Fahrenheit */
} sh_units_t;

/* A temperature in tenths of °C -> tenths of the units. */
int16_t sh_units_temp10(int16_t c10, sh_units_t u);

/* A whole °C -> whole units. */
int16_t sh_units_temp(int16_t c, sh_units_t u);

/* A whole difference in K -> whole units (°F: x 1.8, without +32). */
int16_t sh_units_diff(int16_t k, sh_units_t u);
