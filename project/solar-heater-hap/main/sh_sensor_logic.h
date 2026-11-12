/* Judging the readings of one temperature sensor (doc/operating-modes.md 2.1, item 12) — plain C,
 * no ESP-IDF / FreeRTOS, compiled into the host test too. Temperatures are tenths of °C.
 *
 * A reading fails when the sensor does not answer (or the checksum is wrong), when the value is
 * outside the range of the DS18B20, or when it jumps more than 20 K from the last valid value.
 * A failed reading is followed at once by one more, from a new conversion:
 *   - after a jump the second reading is accepted when it is within 1 K of the first one (the
 *     temperature really changed that fast);
 *   - after any other failure it is accepted when it is good by itself.
 * Otherwise the measurement has failed: the last valid value stays in use and the failures are
 * counted. 5 failed measurements in a row declare the sensor faulty; one good measurement clears
 * both the count and the fault. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_SENSOR_T_MIN         (-550)  /* DS18B20: -55 °C */
#define SH_SENSOR_T_MAX         1250    /* DS18B20: +125 °C */
#define SH_SENSOR_JUMP          200     /* more than this from the last valid value: a jump */
#define SH_SENSOR_JUMP_CONFIRM  10      /* the second reading confirms a jump within this */
#define SH_SENSOR_FAULT_COUNT   5       /* failed measurements in a row */

typedef struct {
    int16_t value;              /* last valid value; SH_TEMP_INVALID before the first one */
    uint8_t fails;              /* failed measurements in a row */
    bool    fault;              /* declared faulty */
} sh_sensor_t;

void sh_sensor_init(sh_sensor_t *s);

/* The first reading of a measurement (`ok`: the sensor answered with a good checksum).
 * Returns true when it is accepted — the measurement is done; false when a second reading from a
 * new conversion is needed, then sh_sensor_second() ends the measurement. */
bool sh_sensor_first(sh_sensor_t *s, bool ok, int16_t t);

/* Both readings of a measurement whose first one was not accepted. Returns true when the second
 * one is accepted, false when the measurement has failed. */
bool sh_sensor_second(sh_sensor_t *s, bool ok1, int16_t t1, bool ok2, int16_t t2);
