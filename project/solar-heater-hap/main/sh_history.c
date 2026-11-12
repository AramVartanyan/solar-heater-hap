/* History: day chart, day, daily summary, log in flash, CSV — see sh_history.h. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarm.h"
#include "sh_history.h"

sh_run_t sh_run_kind(bool pump_on, sh_pump_reason_t reason)
{
    if (!pump_on) {
        return SH_RUN_OFF;
    }
    switch (reason) {
    case SH_PUMP_REASON_DIFF:
    case SH_PUMP_REASON_MIN_RUN:
    case SH_PUMP_REASON_TF_RUN:
    case SH_PUMP_REASON_CF_RUN:
        return SH_RUN_CHARGE;
    case SH_PUMP_REASON_COL_PROTECT:
    case SH_PUMP_REASON_FROST:
    case SH_PUMP_REASON_TF_PROBE:
        return SH_RUN_PROTECT;
    default:
        return SH_RUN_OTHER;
    }
}

/* ---- daily summary ---------------------------------------------------------------------- */

#define PEAK_MIN_MASK   0x07FFu
#define PEAK_PARTIAL    0x4000u
#define PEAK_RELIABLE   0x8000u
#define REC_CRC         (SH_HIST_REC_SIZE - 1)

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint8_t crc8(const uint8_t *p, size_t len)
{
    uint8_t crc = 0xFF;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
        }
    }
    return crc;
}

void sh_hist_encode(const sh_hist_day_t *d, uint8_t rec[SH_HIST_REC_SIZE])
{
    put16(rec + 0, d->day);
    put16(rec + 2, (uint16_t)d->col_max);
    put16(rec + 4, (uint16_t)d->col_min);
    put16(rec + 6, (uint16_t)d->tank_max);
    put16(rec + 8, (uint16_t)d->tank_min);
    for (int k = 0; k < SH_RUN_KINDS; k++) {
        put16(rec + 10 + 2 * k, d->run_min[k]);
    }
    put16(rec + 16, d->cool_min);
    put16(rec + 18, d->heater_min);
    put16(rec + 20, d->alarms);
    put16(rec + 22, (uint16_t)((d->peak_min < 24 * 60 ? d->peak_min : PEAK_MIN_MASK) |
                               (d->partial ? PEAK_PARTIAL : 0) | (d->reliable ? PEAK_RELIABLE : 0)));
    put16(rec + 24, d->sunrise);
    put16(rec + 26, d->sunset);
    rec[28] = d->cool_target;
    rec[29] = SH_HIST_VERSION;
    rec[30] = 0;
    rec[REC_CRC] = crc8(rec, REC_CRC);
}

bool sh_hist_decode(const uint8_t rec[SH_HIST_REC_SIZE], sh_hist_day_t *d)
{
    if (rec[29] != SH_HIST_VERSION || rec[REC_CRC] != crc8(rec, REC_CRC)) {
        return false;
    }
    d->day = get16(rec + 0);
    d->col_max = (int16_t)get16(rec + 2);
    d->col_min = (int16_t)get16(rec + 4);
    d->tank_max = (int16_t)get16(rec + 6);
    d->tank_min = (int16_t)get16(rec + 8);
    for (int k = 0; k < SH_RUN_KINDS; k++) {
        d->run_min[k] = get16(rec + 10 + 2 * k);
    }
    d->cool_min = get16(rec + 16);
    d->heater_min = get16(rec + 18);
    d->alarms = get16(rec + 20);
    const uint16_t peak = get16(rec + 22);
    d->peak_min = (peak & PEAK_MIN_MASK) == PEAK_MIN_MASK ? SH_PEAK_NONE : (peak & PEAK_MIN_MASK);
    d->partial = (peak & PEAK_PARTIAL) != 0;
    d->reliable = (peak & PEAK_RELIABLE) != 0;
    d->sunrise = get16(rec + 24);
    d->sunset = get16(rec + 26);
    d->cool_target = rec[28];
    return true;
}

/* ---- CSV -------------------------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t size, len;
    bool full;
} out_t;

static void put(out_t *o, const char *fmt, ...)
{
    if (o->full) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(o->buf + o->len, o->size - o->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= o->size - o->len) {
        o->full = true;
    } else {
        o->len += (size_t)n;
    }
}

static void put_temp(out_t *o, int16_t t)
{
    if (t != SH_TEMP_INVALID) {
        put(o, "%s%d.%d", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
    }
    put(o, ";");
}

static void put_hhmm(out_t *o, uint16_t minute)
{
    if (minute < 24 * 60) {
        put(o, "%02d:%02d", minute / 60, minute % 60);
    }
    put(o, ";");
}

const char *sh_hist_csv_header(void)
{
    return "date;col_max;col_min;tank_max;tank_min;pump_charge_min;pump_protect_min;pump_other_min;"
           "cooling_min;heater_min;codes;col_peak;clock_reliable;whole_day;sunrise;sunset;"
           "cool_target\r\n";
}

int sh_hist_csv_line(char *buf, size_t size, const sh_hist_day_t *d)
{
    out_t o = { .buf = buf, .size = size };
    if (size == 0) {
        return -1;
    }
    int y, m, day;
    sh_clock_date(d->day, &y, &m, &day);
    put(&o, "%04d-%02d-%02d;", y, m, day);
    put_temp(&o, d->col_max);
    put_temp(&o, d->col_min);
    put_temp(&o, d->tank_max);
    put_temp(&o, d->tank_min);
    put(&o, "%u;%u;%u;%u;%u;", d->run_min[SH_RUN_CHARGE], d->run_min[SH_RUN_PROTECT],
        d->run_min[SH_RUN_OTHER], d->cool_min, d->heater_min);
    bool first = true;
    for (int a = 0; a < SH_ALARM_COUNT; a++) {
        if (d->alarms & SH_ALARM_BIT(a)) {
            put(&o, "%s%s", first ? "" : " ", sh_alarm_name((sh_alarm_t)a));
            first = false;
        }
    }
    put(&o, ";");
    put_hhmm(&o, d->peak_min);
    put(&o, "%d;%d;", d->reliable, !d->partial);
    put_hhmm(&o, d->sunrise);
    put_hhmm(&o, d->sunset);
    if (d->cool_target != SH_HIST_NONE8) {
        put(&o, "%u", d->cool_target);
    }
    put(&o, "\r\n");
    return o.full ? -1 : (int)o.len;
}

/* ---- log in flash ----------------------------------------------------------------------- */

#define PER_SECTOR      (SH_HIST_SECTOR / SH_HIST_REC_SIZE)
#define MAX_SLOTS       2048u           /* 64 KB; a larger partition is used up to here */
#define SCAN_RECS       8               /* records read at once */

static bool rec_empty(const uint8_t *rec)
{
    for (int i = 0; i < SH_HIST_REC_SIZE; i++) {
        if (rec[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

/* Marks the used slots; false: read error. */
static bool scan(const sh_hist_log_t *log, uint8_t *used)
{
    uint8_t buf[SCAN_RECS * SH_HIST_REC_SIZE];
    memset(used, 0, MAX_SLOTS / 8);
    for (uint32_t s = 0; s < log->slots; s += SCAN_RECS) {
        if (!log->flash->read(log->flash->ctx, s * SH_HIST_REC_SIZE, buf, sizeof(buf))) {
            return false;
        }
        for (uint32_t i = 0; i < SCAN_RECS; i++) {
            if (!rec_empty(buf + i * SH_HIST_REC_SIZE)) {
                used[(s + i) / 8] |= (uint8_t)(1u << ((s + i) % 8));
            }
        }
    }
    return true;
}

static bool is_used(const uint8_t *used, uint32_t slot)
{
    return (used[slot / 8] >> (slot % 8)) & 1u;
}

bool sh_hist_log_open(sh_hist_log_t *log, const sh_hist_flash_t *flash)
{
    uint32_t slots = flash->size / SH_HIST_REC_SIZE;
    slots = slots > MAX_SLOTS ? MAX_SLOTS : slots - slots % PER_SECTOR;
    log->flash = flash;
    log->slots = (uint16_t)slots;
    log->head = 0;
    log->count = 0;
    if (slots < 2 * PER_SECTOR) {
        return false;
    }

    uint8_t used[MAX_SLOTS / 8];
    /* every pass but the last one erases a sector, so the loop ends */
    for (uint32_t pass = 0; pass <= slots / PER_SECTOR; pass++) {
        if (!scan(log, used)) {
            return false;
        }
        uint32_t first_used = slots;
        for (uint32_t s = 0; s < slots && first_used == slots; s++) {
            if (is_used(used, s)) {
                first_used = s;
            }
        }
        if (first_used == slots) {
            return true;                        /* empty partition */
        }
        /* the longest run of empty slots, going round from a used one */
        uint32_t best = 0, best_len = 0, run = 0, run_len = 0;
        for (uint32_t k = 1; k <= slots; k++) {
            const uint32_t s = (first_used + k) % slots;
            if (is_used(used, s)) {
                run_len = 0;
            } else {
                if (run_len == 0) {
                    run = s;
                }
                if (++run_len > best_len) {
                    best = run;
                    best_len = run_len;
                }
            }
        }
        if (best_len == 0) {                    /* not a log: no room for the next record */
            return sh_hist_log_clear(log);
        }
        const uint32_t end = (best + best_len) % slots;
        if (end % PER_SECTOR == 0) {            /* the free slots reach the end of their sector */
            log->head = (uint16_t)best;
            log->count = (uint16_t)(slots - best_len);
            return true;
        }
        /* an erase was interrupted: the sector is erased again */
        if (!flash->erase(flash->ctx, end / PER_SECTOR * SH_HIST_SECTOR, SH_HIST_SECTOR)) {
            return false;
        }
    }
    return false;
}

bool sh_hist_log_append(sh_hist_log_t *log, const sh_hist_day_t *d)
{
    const sh_hist_flash_t *f = log->flash;
    if (log->slots == 0) {
        return false;
    }
    const uint16_t next = (uint16_t)((log->head + 1) % log->slots);
    if (log->slots - log->count == 1) {
        /* the last free slot: first the oldest sector (it starts at `next`) is erased, so an
         * empty slot is left also if the power fails right after the write */
        if (!f->erase(f->ctx, (uint32_t)next / PER_SECTOR * SH_HIST_SECTOR, SH_HIST_SECTOR)) {
            return false;
        }
        log->count = (uint16_t)(log->count - PER_SECTOR);
    }
    uint8_t rec[SH_HIST_REC_SIZE], back[SH_HIST_REC_SIZE];
    sh_hist_encode(d, rec);
    const uint32_t off = (uint32_t)log->head * SH_HIST_REC_SIZE;
    const bool ok = f->write(f->ctx, off, rec, sizeof(rec)) && f->read(f->ctx, off, back, sizeof(back)) &&
                    memcmp(rec, back, sizeof(rec)) == 0;
    if (!ok && (!f->read(f->ctx, off, back, sizeof(back)) || rec_empty(back))) {
        return false;                           /* nothing written: the slot stays the next one */
    }
    log->head = next;                           /* a damaged record takes its slot too */
    log->count++;
    return ok;
}

bool sh_hist_log_get(const sh_hist_log_t *log, uint16_t i, sh_hist_day_t *d)
{
    if (i >= log->count) {
        return false;
    }
    const uint32_t slot = ((uint32_t)log->head + log->slots - log->count + i) % log->slots;
    uint8_t rec[SH_HIST_REC_SIZE];
    return log->flash->read(log->flash->ctx, slot * SH_HIST_REC_SIZE, rec, sizeof(rec)) &&
           sh_hist_decode(rec, d);
}

int sh_hist_log_last(const sh_hist_log_t *log, sh_hist_day_t *days, int max)
{
    int n = 0;
    for (uint16_t i = log->count; i-- > 0 && n < max;) {
        if (sh_hist_log_get(log, i, &days[n])) {
            n++;
        }
    }
    return n;
}

bool sh_hist_log_clear(sh_hist_log_t *log)
{
    log->head = 0;
    log->count = 0;
    return log->slots != 0 &&
           log->flash->erase(log->flash->ctx, 0, (uint32_t)log->slots * SH_HIST_REC_SIZE);
}

/* ---- day chart and day ------------------------------------------------------------------ */

#define CLOCK_STEP_S    90      /* the clock may be corrected this much without being a new clock */
#define DST_S           3600

static void day_begin(sh_hist_t *h, uint32_t now_s, bool partial)
{
    h->day_start_s = now_s;
    h->partial = partial;
    h->col_max = h->col_min = h->tank_max = h->tank_min = SH_TEMP_INVALID;
    h->peak_s = now_s;
    memset(h->run_s, 0, sizeof(h->run_s));
    h->alarms = 0;
}

/* The closed intervals that ended less than `age_s` ago, plus the open one. */
static void run_sum(const sh_hist_t *h, uint32_t now_s, uint32_t age_s, uint32_t run_s[SH_RUN_KINDS])
{
    for (int k = 0; k < SH_RUN_KINDS; k++) {
        run_s[k] = h->cur_run_s[k];
    }
    for (int i = 0; i < h->n; i++) {
        if (now_s - h->slot[i].end_s < age_s) {
            for (int k = 0; k < SH_RUN_KINDS; k++) {
                run_s[k] += h->slot[i].run_s[k];
            }
        }
    }
}

static void day_close(sh_hist_t *h, const sh_hist_in_t *in, sh_hist_day_t *d)
{
    memset(d, 0, sizeof(*d));
    d->day = h->day;
    d->col_max = h->col_max;
    d->col_min = h->col_min;
    d->tank_max = h->tank_max;
    d->tank_min = h->tank_min;
    for (int k = 0; k < SH_RUN_KINDS; k++) {
        const uint32_t min = (h->run_s[k] + 30) / 60;
        d->run_min[k] = min > UINT16_MAX ? UINT16_MAX : (uint16_t)min;
    }
    d->alarms = h->alarms;
    d->peak_min = SH_PEAK_NONE;
    if (h->col_max != SH_TEMP_INVALID) {
        const uint32_t before = in->now_s - h->peak_s + in->sec;    /* from the peak to midnight */
        d->peak_min = before >= SH_HIST_DAY_S ? 0 : (uint16_t)((SH_HIST_DAY_S - before) / 60);
    }
    d->partial = h->partial;
    if (!h->partial && d->peak_min != SH_PEAK_NONE) {
        sh_peak_day(&h->peak, h->col_max, h->col_min, d->peak_min);
    }
    d->reliable = h->peak.reliable;
    d->sunrise = d->sunset = SH_HIST_NONE16;
    d->cool_target = SH_HIST_NONE8;
}

bool sh_hist_step(sh_hist_t *h, const sh_hist_in_t *in, sh_hist_day_t *closed)
{
    if (!h->started) {
        h->started = true;
        h->last_s = in->now_s;
        h->last_kind = SH_RUN_OFF;
        h->cur_start_s = in->now_s;
        day_begin(h, in->now_s, true);
        sh_peak_reset(&h->peak, in->zone_confirmed);
    }

    /* the time since the previous step belongs to what the pump did then */
    const uint32_t dt = in->now_s - h->last_s;
    if (h->last_kind != SH_RUN_OFF) {
        const uint32_t cur = h->cur_run_s[h->last_kind] + dt;
        h->cur_run_s[h->last_kind] = cur > UINT16_MAX ? UINT16_MAX : (uint16_t)cur;
        h->run_s[h->last_kind] += dt;
    }

    /* the clock: went on, passed midnight, or is a new one */
    bool new_clock = false, midnight = false, went_back = false, mark = false;
    if (in->clock_set && !h->clock_set) {
        new_clock = true;
    } else if (in->clock_set) {
        const int64_t local = ((int64_t)in->day - h->day) * SH_HIST_DAY_S + (int64_t)in->sec - h->sec;
        const int64_t jump = local - (int64_t)(in->now_s - h->clock_s);
        if (jump < -CLOCK_STEP_S || jump > CLOCK_STEP_S) {
            /* an hour within the same day is the daylight saving time: the day goes on */
            new_clock = in->day != h->day || jump < -(DST_S + CLOCK_STEP_S) || jump > DST_S + CLOCK_STEP_S;
        } else if (in->day == h->day + 1) {
            midnight = true;
        } else if (in->day != h->day) {
            went_back = true;       /* corrected back over midnight: that day is already closed */
        }
        mark = !went_back && in->sec / SH_HIST_SLOT_S != h->sec / SH_HIST_SLOT_S;
    }

    bool day_closed = false;
    if (midnight) {
        day_close(h, in, closed);
        day_closed = true;
        day_begin(h, in->now_s, false);
        h->midnight_s = in->now_s - in->sec;
    } else if (new_clock) {
        sh_peak_reset(&h->peak, in->zone_confirmed);
        h->midnight_s = in->now_s > in->sec ? in->now_s - in->sec : 0;
        for (int i = 0; i < SH_HIST_SLOTS; i++) {
            h->slot[i].min = SH_HIST_NONE16;    /* their local time is worked out again */
        }
        if (in->now_s - h->day_start_s > in->sec) {
            /* what was kept began before this midnight: only the pump times of today stay */
            day_begin(h, in->now_s, true);
            run_sum(h, in->now_s, in->sec, h->run_s);
        }
        h->partial = true;
    }
    if (in->clock_set && !went_back) {
        h->day = in->day;
        h->sec = in->sec;
        h->clock_s = in->now_s;
    }
    h->clock_set = in->clock_set;

    /* the day */
    if (!in->col_fault && in->t_col != SH_TEMP_INVALID) {
        if (h->col_max == SH_TEMP_INVALID || in->t_col > h->col_max) {
            h->col_max = in->t_col;
            h->peak_s = in->now_s;
        }
        if (h->col_min == SH_TEMP_INVALID || in->t_col < h->col_min) {
            h->col_min = in->t_col;
        }
    }
    if (!in->tank_fault && in->t_tank != SH_TEMP_INVALID) {
        if (h->tank_max == SH_TEMP_INVALID || in->t_tank > h->tank_max) {
            h->tank_max = in->t_tank;
        }
        if (h->tank_min == SH_TEMP_INVALID || in->t_tank < h->tank_min) {
            h->tank_min = in->t_tank;
        }
    }
    h->alarms |= in->alarms;

    /* the chart: the interval ends on a 10 min mark of the local time, or after 10 min */
    if (in->now_s != h->cur_start_s && (mark || in->now_s - h->cur_start_s >= SH_HIST_SLOT_S)) {
        sh_hist_slot_t *s = &h->slot[h->head];
        s->end_s = in->now_s;
        s->min = in->clock_set ? (uint16_t)(in->sec / 60) : SH_HIST_NONE16;
        s->t_col = in->col_fault ? SH_TEMP_INVALID : in->t_col;
        s->t_tank = in->tank_fault ? SH_TEMP_INVALID : in->t_tank;
        memcpy(s->run_s, h->cur_run_s, sizeof(s->run_s));
        h->head = (uint8_t)((h->head + 1) % SH_HIST_SLOTS);
        if (h->n < SH_HIST_SLOTS) {
            h->n++;
        }
        h->cur_start_s = in->now_s;
        memset(h->cur_run_s, 0, sizeof(h->cur_run_s));
    }

    h->last_s = in->now_s;
    h->last_kind = sh_run_kind(in->pump_on, in->reason);
    return day_closed;
}

void sh_hist_peak_reset(sh_hist_t *h, bool zone_confirmed)
{
    sh_peak_reset(&h->peak, zone_confirmed);
}

bool sh_hist_reliable(const sh_hist_t *h)
{
    return h->clock_set && h->peak.reliable;
}

/* Local seconds since midnight at the last step. */
static uint32_t sec_now(const sh_hist_t *h)
{
    return h->sec + (h->last_s - h->clock_s);
}

void sh_hist_run_today(const sh_hist_t *h, uint32_t run_s[SH_RUN_KINDS])
{
    if (h->clock_set) {
        memcpy(run_s, h->run_s, sizeof(h->run_s));
    } else {
        run_sum(h, h->last_s, SH_HIST_DAY_S, run_s);
    }
}

int sh_hist_chart(const sh_hist_t *h, sh_hist_point_t *pts, bool *day)
{
    const uint32_t sec = sec_now(h);
    int n = 0;
    *day = h->clock_set;
    for (int i = 0; i < h->n; i++) {
        const sh_hist_slot_t *s = &h->slot[(h->head + SH_HIST_SLOTS - h->n + i) % SH_HIST_SLOTS];
        const uint32_t age = h->last_s - s->end_s;
        if (h->clock_set ? s->end_s <= h->midnight_s : age >= SH_HIST_DAY_S) {
            continue;                   /* before midnight, or older than 24 h */
        }
        sh_hist_point_t *p = &pts[n++];
        if (!h->clock_set) {
            p->x = (int16_t)(age / 60);
        } else if (s->min != SH_HIST_NONE16) {
            p->x = (int16_t)s->min;
        } else {                        /* measured before the clock was set */
            p->x = (int16_t)(sec > age ? (sec - age + 30) / 60 : 0);
        }
        p->t_col = s->t_col;
        p->t_tank = s->t_tank;
        memcpy(p->run_s, s->run_s, sizeof(p->run_s));
    }
    return n;
}
