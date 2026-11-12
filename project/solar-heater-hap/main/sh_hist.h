/* Daily summaries in the flash (doc/web-interface.md 4.1; doc/tasks/step9-clock-history.md 3.3) —
 * plain C, no ESP-IDF / FreeRTOS, compiled into the host test too. The flash is reached through
 * callbacks (sh_history: the "history" partition; the test: an array with the flash rules — erase
 * to 0xFF by sectors, a write only clears bits).
 *
 * Record, 32 B, little-endian; the fields of later groups are kept already ("no data" until then):
 *   0   date, days since 2020-01-01                       u16
 *   2   collector max, min; tank max, min, 0.1 °C         4 x i16 (0x8000: no data)
 *   10  pump run minutes: charging, protection, other     3 x u16
 *   16  night cooling minutes (group C, 0 until then)     u16
 *   18  heater minutes (the heater sign active)           u16
 *   20  alarm codes of the day, SH_ALARM_BIT()            u16
 *   22  collector peak, local minute (bits 0-10, 0x7FF: none); clock reliable (bit 15)   u16
 *   24  learned: sunrise, sunset (local minutes, 0xFFFF: no data)                        2 x u16
 *   28  learned: cooling target, °C (0xFF: no data)       u8
 *   29  version (SH_HIST_VERSION)                         u8
 *   30  checksum: CRC-8 (0x07) of all other bytes         u8
 *   31  reserve; bit 0: partial day                       u8
 * An erased record (all 0xFF) is empty; any other one that fails the version or the checksum is
 * damaged: it keeps its place but is skipped.
 *
 * Ring: the records follow each other; before the last record of a sector is written, the next
 * sector is erased, so there is always an empty record after the newest one (that is how it is
 * found at the start) and the oldest sector is overwritten (64 KB: 1920 - 2048 days). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SH_HIST_REC_SIZE    32
#define SH_HIST_VERSION     1
#define SH_HIST_NO_TEMP     INT16_MIN
#define SH_HIST_NO_TIME     0xFFFFu
#define SH_HIST_NO_U8       0xFFu
#define SH_HIST_NO_PEAK     (-1)

typedef struct {
    uint16_t day;
    int16_t col_max, col_min, tank_max, tank_min;   /* SH_HIST_NO_TEMP */
    uint16_t run_min[3];                            /* charging, protection, other (sh_run_kind_t) */
    uint16_t cool_min;
    uint16_t heater_min;
    uint16_t alarms;
    int16_t peak_min;                               /* SH_HIST_NO_PEAK */
    bool clock_reliable;
    uint16_t sunrise, sunset;                       /* SH_HIST_NO_TIME */
    uint8_t cool_target;                            /* SH_HIST_NO_U8 */
    bool partial;
} sh_hist_rec_t;

/* A record with "no data" in every learned field (and 0 in the counters of later groups). */
void sh_hist_rec_init(sh_hist_rec_t *r, uint16_t day);

void sh_hist_encode(const sh_hist_rec_t *r, uint8_t out[SH_HIST_REC_SIZE]);

typedef enum {
    SH_HIST_EMPTY = 0,
    SH_HIST_VALID,
    SH_HIST_DAMAGED,
} sh_hist_slot_t;

sh_hist_slot_t sh_hist_decode(const uint8_t in[SH_HIST_REC_SIZE], sh_hist_rec_t *r);

/* The flash. `size` is a multiple of `sector`, which is a multiple of SH_HIST_REC_SIZE; offsets
 * are from the start of the area. The callbacks return false on an error. */
typedef struct {
    bool (*read)(void *ctx, uint32_t off, void *buf, uint32_t len);
    bool (*write)(void *ctx, uint32_t off, const void *buf, uint32_t len);
    bool (*erase)(void *ctx, uint32_t off, uint32_t len);
    void *ctx;
    uint32_t size;
    uint32_t sector;
} sh_hist_flash_t;

typedef struct {
    const sh_hist_flash_t *f;
    uint32_t slots;             /* records in the area */
    uint32_t head;              /* the next record to write (empty) */
} sh_hist_t;

/* Finds the newest record; an area without an empty record (never used, unknown content) is
 * erased. False on a flash error. */
bool sh_hist_open(sh_hist_t *h, const sh_hist_flash_t *f);

/* Writes a record after the newest one. False on a flash error (the place is used anyway). */
bool sh_hist_append(sh_hist_t *h, const sh_hist_rec_t *r);

/* Erases everything. */
bool sh_hist_clear(sh_hist_t *h);

/* The valid records, newest first or oldest first, at most `max` (0: all); `fn` returns false to
 * stop. Returns how many were given. */
typedef bool (*sh_hist_fn)(void *ctx, const sh_hist_rec_t *r);
unsigned sh_hist_walk(const sh_hist_t *h, bool newest_first, unsigned max, sh_hist_fn fn, void *ctx);

/* CSV ("Download the history"): separator ';', decimal point, the header in English; dates
 * YYYY-MM-DD, temperatures in °C with one decimal, times HH:MM, alarms as codes ("E1 Hot"); no
 * data -> an empty field. Each line ends with "\r\n". The line functions return the length, or -1
 * if `size` is too small. */
#define SH_HIST_CSV_LINE_MAX    160
extern const char sh_hist_csv_header[];
int sh_hist_csv_line(char *buf, size_t size, const sh_hist_rec_t *r);
