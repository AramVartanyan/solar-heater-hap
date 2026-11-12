/* Daily summaries in the flash — see sh_hist.h. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sh_alarm.h"
#include "sh_clock_rules.h"
#include "sh_hist.h"

#define PEAK_MASK       0x07FFu
#define PEAK_RELIABLE   0x8000u
#define RES_PARTIAL     0x01u
#define OFF_VERSION     29
#define OFF_CRC         30
#define OFF_RESERVE     31

void sh_hist_rec_init(sh_hist_rec_t *r, uint16_t day)
{
    memset(r, 0, sizeof(*r));
    r->day = day;
    r->col_max = r->col_min = r->tank_max = r->tank_min = SH_HIST_NO_TEMP;
    r->peak_min = SH_HIST_NO_PEAK;
    r->sunrise = r->sunset = SH_HIST_NO_TIME;
    r->cool_target = SH_HIST_NO_U8;
}

static uint8_t crc8(const uint8_t *p)
{
    uint8_t crc = 0;
    for (int i = 0; i < SH_HIST_REC_SIZE; i++) {
        if (i == OFF_CRC) {
            continue;
        }
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (uint8_t)(crc & 0x80 ? (crc << 1) ^ 0x07 : crc << 1);
        }
    }
    return crc;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

void sh_hist_encode(const sh_hist_rec_t *r, uint8_t out[SH_HIST_REC_SIZE])
{
    put16(out + 0, r->day);
    put16(out + 2, (uint16_t)r->col_max);
    put16(out + 4, (uint16_t)r->col_min);
    put16(out + 6, (uint16_t)r->tank_max);
    put16(out + 8, (uint16_t)r->tank_min);
    for (int k = 0; k < 3; k++) {
        put16(out + 10 + 2 * k, r->run_min[k]);
    }
    put16(out + 16, r->cool_min);
    put16(out + 18, r->heater_min);
    put16(out + 20, r->alarms);
    uint16_t peak = r->peak_min >= 0 && r->peak_min < 1440 ? (uint16_t)r->peak_min : PEAK_MASK;
    if (r->clock_reliable) {
        peak |= PEAK_RELIABLE;
    }
    put16(out + 22, peak);
    put16(out + 24, r->sunrise);
    put16(out + 26, r->sunset);
    out[28] = r->cool_target;
    out[OFF_VERSION] = SH_HIST_VERSION;
    out[OFF_RESERVE] = r->partial ? RES_PARTIAL : 0;
    out[OFF_CRC] = crc8(out);
}

static bool is_empty(const uint8_t *p)
{
    for (int i = 0; i < SH_HIST_REC_SIZE; i++) {
        if (p[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

sh_hist_slot_t sh_hist_decode(const uint8_t in[SH_HIST_REC_SIZE], sh_hist_rec_t *r)
{
    if (is_empty(in)) {
        return SH_HIST_EMPTY;
    }
    if (in[OFF_VERSION] != SH_HIST_VERSION || in[OFF_CRC] != crc8(in)) {
        return SH_HIST_DAMAGED;
    }
    r->day = get16(in + 0);
    r->col_max = (int16_t)get16(in + 2);
    r->col_min = (int16_t)get16(in + 4);
    r->tank_max = (int16_t)get16(in + 6);
    r->tank_min = (int16_t)get16(in + 8);
    for (int k = 0; k < 3; k++) {
        r->run_min[k] = get16(in + 10 + 2 * k);
    }
    r->cool_min = get16(in + 16);
    r->heater_min = get16(in + 18);
    r->alarms = get16(in + 20);
    const uint16_t peak = get16(in + 22);
    r->peak_min = (peak & PEAK_MASK) < 1440 ? (int16_t)(peak & PEAK_MASK) : SH_HIST_NO_PEAK;
    r->clock_reliable = (peak & PEAK_RELIABLE) != 0;
    r->sunrise = get16(in + 24);
    r->sunset = get16(in + 26);
    r->cool_target = in[28];
    r->partial = (in[OFF_RESERVE] & RES_PARTIAL) != 0;
    return SH_HIST_VALID;
}

/* ---- ring ------------------------------------------------------------------------------- */

static sh_hist_slot_t read_slot(const sh_hist_t *h, uint32_t i, sh_hist_rec_t *r, bool *ok)
{
    uint8_t buf[SH_HIST_REC_SIZE];
    if (!h->f->read(h->f->ctx, i * SH_HIST_REC_SIZE, buf, sizeof(buf))) {
        *ok = false;
        return SH_HIST_DAMAGED;
    }
    return sh_hist_decode(buf, r);
}

static bool erase_sector_of(const sh_hist_t *h, uint32_t i)
{
    const uint32_t off = i * SH_HIST_REC_SIZE / h->f->sector * h->f->sector;
    return h->f->erase(h->f->ctx, off, h->f->sector);
}

bool sh_hist_open(sh_hist_t *h, const sh_hist_flash_t *f)
{
    h->f = f;
    h->slots = f->size / SH_HIST_REC_SIZE;
    h->head = 0;
    bool ok = true;

    /* the head: an empty record after a used one; of several (unknown content), the one after the
     * newest valid record */
    sh_hist_rec_t r;
    sh_hist_slot_t prev = read_slot(h, h->slots - 1, &r, &ok);
    sh_hist_rec_t prev_rec = r;
    bool any_empty = false, found = false;
    int best_day = -1;
    for (uint32_t i = 0; i < h->slots && ok; i++) {
        const sh_hist_slot_t cur = read_slot(h, i, &r, &ok);
        if (cur == SH_HIST_EMPTY) {
            any_empty = true;
            if (prev != SH_HIST_EMPTY) {
                const int day = prev == SH_HIST_VALID ? prev_rec.day : -1;
                if (!found || day > best_day) {
                    h->head = i;
                    best_day = day;
                    found = true;
                }
            }
        }
        prev = cur;
        prev_rec = r;
    }
    if (!ok) {
        return false;
    }
    if (!any_empty) {
        return sh_hist_clear(h);            /* never used: unknown content */
    }
    return true;
}

bool sh_hist_append(sh_hist_t *h, const sh_hist_rec_t *r)
{
    const uint32_t per_sector = h->f->sector / SH_HIST_REC_SIZE;
    bool ok = true;
    /* A place that is not empty (a bit lost in the free area, a record cut by a power failure)
     * cannot be written over: it is left as it is (skipped as damaged) and the next place is taken.
     * Erasing its sector instead would lose the valid records next to it. */
    for (uint32_t tries = 0; tries < h->slots; tries++) {
        const uint32_t i = h->head;
        h->head = (i + 1) % h->slots;
        if ((i + 1) % per_sector == 0) {
            ok = erase_sector_of(h, h->head) && ok;     /* an empty record after the newest */
        }
        bool read_ok = true;
        sh_hist_rec_t old;
        const sh_hist_slot_t slot = read_slot(h, i, &old, &read_ok);
        if (!read_ok) {
            return false;
        }
        if (slot != SH_HIST_EMPTY) {
            continue;
        }
        uint8_t buf[SH_HIST_REC_SIZE];
        sh_hist_encode(r, buf);
        return h->f->write(h->f->ctx, i * SH_HIST_REC_SIZE, buf, sizeof(buf)) && ok;
    }
    return false;
}

bool sh_hist_clear(sh_hist_t *h)
{
    h->head = 0;
    return h->f->erase(h->f->ctx, 0, h->f->size);
}

unsigned sh_hist_walk(const sh_hist_t *h, bool newest_first, unsigned max, sh_hist_fn fn, void *ctx)
{
    unsigned n = 0;
    bool ok = true;
    for (uint32_t k = 1; k < h->slots && ok && (max == 0 || n < max); k++) {
        const uint32_t i = newest_first ? (h->head + h->slots - k) % h->slots : (h->head + k) % h->slots;
        sh_hist_rec_t r;
        if (read_slot(h, i, &r, &ok) == SH_HIST_VALID) {
            n++;
            if (!fn(ctx, &r)) {
                break;
            }
        }
    }
    return n;
}

/* ---- CSV -------------------------------------------------------------------------------- */

const char sh_hist_csv_header[] =
    "date;collector_max;collector_min;tank_max;tank_min;pump_charging_min;pump_protection_min;"
    "pump_other_min;night_cooling_min;heater_min;alarms;collector_peak;clock_reliable;partial_day;"
    "sunrise;sunset;cooling_target\r\n";

typedef struct {
    char *buf;
    size_t size, len;
    bool full;
} line_t;

static void add(line_t *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void add(line_t *l, const char *fmt, ...)
{
    if (l->full) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(l->buf + l->len, l->size - l->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= l->size - l->len) {
        l->full = true;
    } else {
        l->len += (size_t)n;
    }
}

static void add_temp(line_t *l, int16_t t)
{
    if (t == SH_HIST_NO_TEMP) {
        add(l, ";");
    } else {
        add(l, ";%s%d.%d", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
    }
}

static void add_time(line_t *l, int v)
{
    if (v < 0 || v >= 1440) {
        add(l, ";");
    } else {
        add(l, ";%02d:%02d", v / 60, v % 60);
    }
}

int sh_hist_csv_line(char *buf, size_t size, const sh_hist_rec_t *r)
{
    line_t l = { .buf = buf, .size = size };
    if (size == 0) {
        return -1;
    }
    int y, m, d;
    sh_day_date(r->day, &y, &m, &d);
    add(&l, "%04d-%02d-%02d", y, m, d);
    add_temp(&l, r->col_max);
    add_temp(&l, r->col_min);
    add_temp(&l, r->tank_max);
    add_temp(&l, r->tank_min);
    add(&l, ";%u;%u;%u;%u;%u;", r->run_min[0], r->run_min[1], r->run_min[2], r->cool_min, r->heater_min);
    bool first = true;
    for (int a = 0; a < SH_ALARM_COUNT; a++) {
        if (r->alarms & SH_ALARM_BIT(a)) {
            add(&l, "%s%s", first ? "" : " ", sh_alarm_name((sh_alarm_t)a));
            first = false;
        }
    }
    add_time(&l, r->peak_min);
    add(&l, ";%d;%d", r->clock_reliable, r->partial);
    add_time(&l, r->sunrise == SH_HIST_NO_TIME ? -1 : r->sunrise);
    add_time(&l, r->sunset == SH_HIST_NO_TIME ? -1 : r->sunset);
    if (r->cool_target == SH_HIST_NO_U8) {
        add(&l, ";\r\n");
    } else {
        add(&l, ";%u\r\n", r->cool_target);
    }
    return l.full ? -1 : (int)l.len;
}
