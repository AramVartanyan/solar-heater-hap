/* Plausibility of the sensor readings and the sensor fault (doc/operating-modes.md 5.1)
 * — plain C, compiled into the host test too.
 *
 * A reading fails when the sensor does not answer, the CRC is wrong (both: `ok` false), the value
 * is outside the DS18B20 range (-55 ... +125 °C) or it jumps more than 20 K from the last valid
 * value. Then a second reading, with a new conversion, is taken at once; after a jump it is
 * accepted within 1 K of the jumped value (a real fast change). A measurement fails when both
 * readings fail: the last valid value is kept, and 5 failed measurements in a row make the sensor
 * faulty; a good measurement clears it. Temperatures in tenths of °C. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SH_MEAS_MIN         (-550)      /* DS18B20 range */
#define SH_MEAS_MAX         1250
#define SH_MEAS_JUMP        200         /* a change of more than 20 K is a jump */
#define SH_MEAS_CONFIRM     10          /* the second reading confirms a jump within 1 K */
#define SH_MEAS_FAULT_COUNT 5           /* failed measurements in a row -> faulty */

typedef struct {
    int16_t last;           /* last valid value; SH_TEMP_INVALID before the first one */
    uint8_t fails;          /* failed measurements in a row */
    bool faulty;
} sh_meas_t;

void sh_meas_init(sh_meas_t *m);

/* First reading of a measurement: true = accepted, false = take a second reading. */
bool sh_meas_accept(const sh_meas_t *m, bool ok, int16_t t);

/* Second reading, after the first one (`ok1`, `t1`) was refused: true = accepted. */
bool sh_meas_accept_retry(const sh_meas_t *m, bool ok1, int16_t t1, bool ok2, int16_t t2);

/* End of a measurement: `success` with the accepted value `t`, or failed. Returns true when the
 * fault state changed. */
bool sh_meas_update(sh_meas_t *m, bool success, int16_t t);
