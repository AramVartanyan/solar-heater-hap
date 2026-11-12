/* Host test of the history (main/sh_history.c; doc/tasks/step9-clock-history.md 3.2, 3.3): the day
 * chart, the day with its summary at midnight and the time zone check, the 32-byte record, the log
 * in flash (ring, power loss, damaged record), the CSV. */

#include <string.h>
#include "test_util.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_history.h"

/* ---- record and CSV --------------------------------------------------------------------- */

static void test_run_kind(void)
{
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_DIFF), SH_RUN_OFF);
    CHECK_EQ(sh_run_kind(false, SH_PUMP_REASON_MIN_OFF), SH_RUN_OFF);
    /* charging */
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_DIFF), SH_RUN_CHARGE);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_MIN_RUN), SH_RUN_CHARGE);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_TF_RUN), SH_RUN_CHARGE);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_CF_RUN), SH_RUN_CHARGE);
    /* protection: the collector, the frost */
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_COL_PROTECT), SH_RUN_PROTECT);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_FROST), SH_RUN_PROTECT);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_TF_PROBE), SH_RUN_PROTECT);
    /* other: test, anti-seize, probe at a faulty collector sensor */
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_TEST), SH_RUN_OTHER);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_ANTI_SEIZE), SH_RUN_OTHER);
    CHECK_EQ(sh_run_kind(true, SH_PUMP_REASON_CF_PROBE), SH_RUN_OTHER);
}

static sh_hist_day_t sample_day(uint16_t day)
{
    const sh_hist_day_t d = {
        .day = day, .col_max = 783, .col_min = -21, .tank_max = 624, .tank_min = 407,
        .run_min = { 192, 4, 1 }, .alarms = SH_ALARM_BIT(SH_ALARM_E1) | SH_ALARM_BIT(SH_ALARM_HOT),
        .peak_min = 13 * 60 + 10, .reliable = true,
        .sunrise = SH_HIST_NONE16, .sunset = SH_HIST_NONE16, .cool_target = SH_HIST_NONE8,
    };
    return d;
}

static bool same_day(const sh_hist_day_t *a, const sh_hist_day_t *b)
{
    return a->day == b->day && a->col_max == b->col_max && a->col_min == b->col_min &&
           a->tank_max == b->tank_max && a->tank_min == b->tank_min &&
           memcmp(a->run_min, b->run_min, sizeof(a->run_min)) == 0 && a->cool_min == b->cool_min &&
           a->heater_min == b->heater_min && a->alarms == b->alarms && a->peak_min == b->peak_min &&
           a->partial == b->partial && a->reliable == b->reliable && a->sunrise == b->sunrise &&
           a->sunset == b->sunset && a->cool_target == b->cool_target;
}

static void test_record(void)
{
    uint8_t rec[SH_HIST_REC_SIZE];
    sh_hist_day_t d = sample_day(2506), back;
    CHECK_EQ(sizeof(rec), 32);
    sh_hist_encode(&d, rec);

    /* the fixed format, little-endian */
    const uint8_t expect[SH_HIST_REC_SIZE - 1] = {
        0xCA, 0x09,                                     /* day 2506 = 2026-11-11 */
        0x0F, 0x03, 0xEB, 0xFF, 0x70, 0x02, 0x97, 0x01, /* 78.3, -2.1, 62.4, 40.7 */
        192, 0, 4, 0, 1, 0,                             /* pump minutes */
        0, 0, 0, 0,                                     /* cooling, heater */
        0x21, 0x00,                                     /* E1, Hot */
        0x16, 0x83,                                     /* 13:10, reliable */
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF,                   /* learned: no data */
        SH_HIST_VERSION, 0,
    };
    CHECK(memcmp(rec, expect, sizeof(expect)) == 0);
    CHECK(sh_hist_decode(rec, &back));
    CHECK(same_day(&d, &back));

    /* every field goes there and back */
    d = (sh_hist_day_t){
        .day = 65535, .col_max = 2000, .col_min = SH_TEMP_INVALID, .tank_max = -550, .tank_min = 1250,
        .run_min = { 1440, 65535, 7 }, .cool_min = 300, .heater_min = 45, .alarms = 0xFFFF,
        .peak_min = 23 * 60 + 59, .partial = true, .reliable = false, .sunrise = 371, .sunset = 1203,
        .cool_target = 45,
    };
    sh_hist_encode(&d, rec);
    CHECK(sh_hist_decode(rec, &back));
    CHECK(same_day(&d, &back));
    d.peak_min = SH_PEAK_NONE;                          /* no collector value that day */
    d.partial = false;
    d.reliable = true;
    sh_hist_encode(&d, rec);
    CHECK(sh_hist_decode(rec, &back));
    CHECK(same_day(&d, &back));
    CHECK_EQ(back.peak_min, SH_PEAK_NONE);

    /* the checksum catches every single wrong bit */
    for (int bit = 0; bit < SH_HIST_REC_SIZE * 8; bit++) {
        rec[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        CHECK(!sh_hist_decode(rec, &back));
        rec[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
    CHECK(sh_hist_decode(rec, &back));

    /* another version, an empty slot, zeros */
    rec[29] = SH_HIST_VERSION + 1;
    CHECK(!sh_hist_decode(rec, &back));
    memset(rec, 0xFF, sizeof(rec));
    CHECK(!sh_hist_decode(rec, &back));
    memset(rec, 0, sizeof(rec));
    CHECK(!sh_hist_decode(rec, &back));
}

static void test_csv(void)
{
    char line[SH_HIST_CSV_LINE_MAX];
    CHECK(strcmp(sh_hist_csv_header(),
        "date;col_max;col_min;tank_max;tank_min;pump_charge_min;pump_protect_min;pump_other_min;"
        "cooling_min;heater_min;codes;col_peak;clock_reliable;whole_day;sunrise;sunset;"
        "cool_target\r\n") == 0);

    /* ';' between the fields, decimal point */
    sh_hist_day_t d = sample_day(2506);
    int len = sh_hist_csv_line(line, sizeof(line), &d);
    CHECK_EQ(len, (int)strlen(line));
    CHECK(strcmp(line, "2026-11-11;78.3;-2.1;62.4;40.7;192;4;1;0;0;E1 Hot;13:10;1;1;;;\r\n") == 0);

    /* as many fields as the header */
    int fields = 0, heads = 0;
    for (const char *p = line; *p; p++) {
        fields += *p == ';';
    }
    for (const char *p = sh_hist_csv_header(); *p; p++) {
        heads += *p == ';';
    }
    CHECK_EQ(fields, heads);

    /* no values, a day that is not whole, an unreliable clock; later groups */
    d = (sh_hist_day_t){
        .day = 59, .col_max = SH_TEMP_INVALID, .col_min = SH_TEMP_INVALID, .tank_max = -5, .tank_min = -105,
        .peak_min = SH_PEAK_NONE, .partial = true, .cool_min = 120, .heater_min = 30,
        .sunrise = 6 * 60 + 5, .sunset = 20 * 60 + 41, .cool_target = 45,
    };
    len = sh_hist_csv_line(line, sizeof(line), &d);
    CHECK(len > 0 && strcmp(line, "2020-02-29;;;-0.5;-10.5;0;0;0;120;30;;;0;0;06:05;20:41;45\r\n") == 0);

    /* the longest line fits */
    d = (sh_hist_day_t){
        .day = 65535, .col_max = -1234, .col_min = -1234, .tank_max = -1234, .tank_min = -1234,
        .run_min = { 65535, 65535, 65535 }, .cool_min = 65535, .heater_min = 65535, .alarms = 0xFFFF,
        .peak_min = 1439, .sunrise = 1439, .sunset = 1439, .cool_target = 254,
    };
    len = sh_hist_csv_line(line, sizeof(line), &d);
    CHECK(len > 0 && len < SH_HIST_CSV_LINE_MAX - 16);
    CHECK_EQ(sh_hist_csv_line(line, 20, &d), -1);
}

/* ---- log in flash ----------------------------------------------------------------------- */

#define FLASH_SIZE      0x10000u                        /* the "history" partition */
#define SECTORS         (FLASH_SIZE / SH_HIST_SECTOR)
#define PER_SECTOR      (SH_HIST_SECTOR / SH_HIST_REC_SIZE)
#define SLOTS           (FLASH_SIZE / SH_HIST_REC_SIZE)

/* NOR flash in RAM: an erase sets the bits of whole sectors, a write only clears bits. */
typedef struct {
    uint8_t mem[FLASH_SIZE];
    unsigned erases[SECTORS];
    unsigned writes;
    int fail_writes;            /* this many writes from now on fail without writing */
    bool fail_all;
} ram_flash_t;

static bool ram_read(void *ctx, uint32_t off, uint8_t *buf, uint32_t len)
{
    ram_flash_t *f = ctx;
    if (f->fail_all || off + len > FLASH_SIZE) {
        return false;
    }
    memcpy(buf, f->mem + off, len);
    return true;
}

static bool ram_write(void *ctx, uint32_t off, const uint8_t *buf, uint32_t len)
{
    ram_flash_t *f = ctx;
    if (f->fail_all || off + len > FLASH_SIZE) {
        return false;
    }
    if (f->fail_writes > 0) {
        f->fail_writes--;
        return false;
    }
    for (uint32_t i = 0; i < len; i++) {
        f->mem[off + i] &= buf[i];
    }
    f->writes++;
    return true;
}

static bool ram_erase(void *ctx, uint32_t off, uint32_t len)
{
    ram_flash_t *f = ctx;
    if (f->fail_all || off % SH_HIST_SECTOR || len % SH_HIST_SECTOR || off + len > FLASH_SIZE) {
        return false;
    }
    memset(f->mem + off, 0xFF, len);
    for (uint32_t s = off / SH_HIST_SECTOR; s < (off + len) / SH_HIST_SECTOR; s++) {
        f->erases[s]++;
    }
    return true;
}

static ram_flash_t s_ram;
static const sh_hist_flash_t s_flash = {
    .ctx = &s_ram, .size = FLASH_SIZE, .read = ram_read, .write = ram_write, .erase = ram_erase,
};

static void flash_new(void)
{
    memset(&s_ram, 0, sizeof(s_ram));
    memset(s_ram.mem, 0xFF, sizeof(s_ram.mem));
}

/* Records with the days first ... last are stored, in this order, and nothing else. */
static void check_days(const sh_hist_log_t *log, unsigned first, unsigned last)
{
    CHECK_EQ(log->count, last - first + 1);
    for (unsigned i = 0; i < log->count; i++) {
        sh_hist_day_t d;
        if (!sh_hist_log_get(log, (uint16_t)i, &d) || d.day != first + i) {
            CHECK(sh_hist_log_get(log, (uint16_t)i, &d));
            CHECK_EQ(d.day, first + i);
            return;
        }
    }
    sh_hist_day_t d;
    CHECK(!sh_hist_log_get(log, log->count, &d));
}

/* What a restart finds is what the running firmware has. */
static void check_reopen(const sh_hist_log_t *log)
{
    sh_hist_log_t again;
    CHECK(sh_hist_log_open(&again, &s_flash));
    CHECK_EQ(again.slots, log->slots);
    CHECK_EQ(again.head, log->head);
    CHECK_EQ(again.count, log->count);
}

static void append_days(sh_hist_log_t *log, unsigned first, unsigned last)
{
    for (unsigned day = first; day <= last; day++) {
        const sh_hist_day_t d = sample_day((uint16_t)day);
        if (!sh_hist_log_append(log, &d)) {
            CHECK(sh_hist_log_append(log, &d));
            return;
        }
    }
}

static void test_log_basic(void)
{
    sh_hist_log_t log;
    sh_hist_day_t d, days[SH_HIST_WEB_DAYS];
    flash_new();
    CHECK(sh_hist_log_open(&log, &s_flash));
    CHECK_EQ(log.slots, 2048);
    CHECK_EQ(log.count, 0);
    CHECK_EQ(log.head, 0);
    CHECK(!sh_hist_log_get(&log, 0, &d));
    CHECK_EQ(sh_hist_log_last(&log, days, SH_HIST_WEB_DAYS), 0);

    append_days(&log, 100, 100);
    check_days(&log, 100, 100);
    CHECK_EQ(sh_hist_log_last(&log, days, SH_HIST_WEB_DAYS), 1);
    CHECK_EQ(days[0].day, 100);
    check_reopen(&log);

    append_days(&log, 101, 109);
    check_days(&log, 100, 109);
    check_reopen(&log);
    CHECK_EQ(s_ram.writes, 10);                         /* one write a day, nothing erased */
    for (unsigned s = 0; s < SECTORS; s++) {
        CHECK_EQ(s_ram.erases[s], 0);
    }

    /* the last 7, the newest first */
    CHECK_EQ(sh_hist_log_last(&log, days, SH_HIST_WEB_DAYS), 7);
    for (int i = 0; i < 7; i++) {
        CHECK_EQ(days[i].day, 109 - i);
    }
    CHECK_EQ(sh_hist_log_last(&log, days, 3), 3);
    CHECK_EQ(days[2].day, 107);

    /* reset of the history */
    CHECK(sh_hist_log_clear(&log));
    CHECK_EQ(log.count, 0);
    check_reopen(&log);
    append_days(&log, 7, 8);
    check_days(&log, 7, 8);
}

/* The ring: as many as fit; the oldest sector goes when the last free slot is written. */
static void test_log_ring(void)
{
    sh_hist_log_t log;
    flash_new();
    CHECK(sh_hist_log_open(&log, &s_flash));

    append_days(&log, 1, SLOTS - 1);                    /* one slot stays free */
    check_days(&log, 1, SLOTS - 1);
    CHECK_EQ(log.count, 2047);
    check_reopen(&log);
    CHECK_EQ(s_ram.erases[0], 0);

    append_days(&log, SLOTS, SLOTS);                    /* the oldest 128 days go */
    CHECK_EQ(s_ram.erases[0], 1);
    check_days(&log, PER_SECTOR + 1, SLOTS);
    CHECK_EQ(log.count, 1920);
    check_reopen(&log);

    append_days(&log, SLOTS + 1, SLOTS + PER_SECTOR - 1);
    check_days(&log, PER_SECTOR + 1, SLOTS + PER_SECTOR - 1);
    CHECK_EQ(log.count, 2047);
    check_reopen(&log);
    CHECK_EQ(s_ram.erases[1], 0);

    /* years on: every state is found again after a restart */
    unsigned day = SLOTS + PER_SECTOR;
    for (; day < 3 * SLOTS + 77; day++) {
        append_days(&log, day, day);
        sh_hist_log_t again;
        if (!sh_hist_log_open(&again, &s_flash) || again.head != log.head || again.count != log.count ||
            log.count < SLOTS - PER_SECTOR || log.count > SLOTS - 1) {
            CHECK(false);
            break;
        }
    }
    check_days(&log, day - log.count, day - 1);
    sh_hist_day_t days[SH_HIST_WEB_DAYS];
    CHECK_EQ(sh_hist_log_last(&log, days, SH_HIST_WEB_DAYS), 7);
    CHECK_EQ(days[0].day, day - 1);
    CHECK_EQ(days[6].day, day - 7);

    /* wear: a sector is erased once in 2048 days (16 sectors x 128 days) */
    for (unsigned s = 0; s < SECTORS; s++) {
        CHECK(s_ram.erases[s] >= 2 && s_ram.erases[s] <= 3);
    }
    CHECK_EQ(s_ram.writes, day - 1);
}

static void test_log_damage(void)
{
    sh_hist_log_t log;
    sh_hist_day_t d, days[SH_HIST_WEB_DAYS];
    flash_new();
    CHECK(sh_hist_log_open(&log, &s_flash));
    append_days(&log, 1, 300);

    /* a damaged record: left out, the others stay */
    s_ram.mem[297 * SH_HIST_REC_SIZE + 5] ^= 0x10;      /* day 298 */
    s_ram.mem[100 * SH_HIST_REC_SIZE + 31] = 0;         /* day 101 */
    CHECK(sh_hist_log_open(&log, &s_flash));
    CHECK_EQ(log.count, 300);
    CHECK(!sh_hist_log_get(&log, 297, &d));
    CHECK(!sh_hist_log_get(&log, 100, &d));
    CHECK(sh_hist_log_get(&log, 296, &d) && d.day == 297);
    CHECK(sh_hist_log_get(&log, 298, &d) && d.day == 299);
    CHECK_EQ(sh_hist_log_last(&log, days, SH_HIST_WEB_DAYS), 7);
    const uint16_t expect[] = { 300, 299, 297, 296, 295, 294, 293 };
    for (int i = 0; i < 7; i++) {
        CHECK_EQ(days[i].day, expect[i]);
    }
    append_days(&log, 301, 301);
    CHECK(sh_hist_log_get(&log, 300, &d) && d.day == 301);

    /* the power failed in the middle of a record: its slot is taken, the next record follows */
    uint8_t rec[SH_HIST_REC_SIZE];
    d = sample_day(302);
    sh_hist_encode(&d, rec);
    memcpy(s_ram.mem + 301 * SH_HIST_REC_SIZE, rec, 13);
    CHECK(sh_hist_log_open(&log, &s_flash));
    CHECK_EQ(log.head, 302);
    CHECK_EQ(log.count, 302);
    CHECK(!sh_hist_log_get(&log, 301, &d));
    append_days(&log, 303, 303);
    CHECK(sh_hist_log_get(&log, 302, &d) && d.day == 303);
    CHECK_EQ(sh_hist_log_last(&log, days, 2), 2);
    CHECK(days[0].day == 303 && days[1].day == 301);

    /* an empty slot that lost a bit: a damaged record, the free slots begin after it */
    s_ram.mem[310 * SH_HIST_REC_SIZE + 3] = 0xFE;
    CHECK(sh_hist_log_open(&log, &s_flash));
    CHECK_EQ(log.head, 311);
    append_days(&log, 304, 304);
    CHECK_EQ(sh_hist_log_last(&log, days, 2), 2);
    CHECK(days[0].day == 304 && days[1].day == 303);
}

static void test_log_power_loss(void)
{
    sh_hist_log_t log, again;
    sh_hist_day_t d;

    /* full ring; the power fails after the erase of the oldest sector, before the write */
    flash_new();
    CHECK(sh_hist_log_open(&log, &s_flash));
    append_days(&log, 1, SLOTS - 1);
    s_ram.fail_writes = 2;
    d = sample_day(SLOTS);
    CHECK(!sh_hist_log_append(&log, &d));
    s_ram.fail_writes = 0;
    CHECK_EQ(s_ram.erases[0], 1);
    CHECK(sh_hist_log_open(&again, &s_flash));
    CHECK_EQ(again.head, SLOTS - 1);
    check_days(&again, PER_SECTOR + 1, SLOTS - 1);
    check_days(&log, PER_SECTOR + 1, SLOTS - 1);        /* the running firmware agrees */
    append_days(&again, SLOTS, SLOTS + 5);
    check_days(&again, PER_SECTOR + 1, SLOTS + 5);
    CHECK_EQ(s_ram.erases[0], 1);                       /* not erased twice */
    CHECK_EQ(s_ram.erases[1], 0);

    /* ... or in the middle of that erase: the sector is erased again */
    flash_new();
    CHECK(sh_hist_log_open(&log, &s_flash));
    append_days(&log, 1, SLOTS - 1);
    memset(s_ram.mem, 0xFF, 1000);                      /* sector 0 half erased */
    memset(s_ram.mem + 1000, 0xA5, 200);
    CHECK(sh_hist_log_open(&again, &s_flash));
    CHECK_EQ(s_ram.erases[0], 1);
    CHECK_EQ(again.head, SLOTS - 1);
    check_days(&again, PER_SECTOR + 1, SLOTS - 1);
    append_days(&again, SLOTS, SLOTS);
    check_days(&again, PER_SECTOR + 1, SLOTS);

    /* a write that fails: nothing changes, the next one takes the same slot */
    flash_new();
    CHECK(sh_hist_log_open(&log, &s_flash));
    append_days(&log, 1, 5);
    s_ram.fail_writes = 1;
    d = sample_day(6);
    CHECK(!sh_hist_log_append(&log, &d));
    check_days(&log, 1, 5);
    append_days(&log, 6, 6);
    check_days(&log, 1, 6);
    check_reopen(&log);

    /* content that is not a log (no free slot): erased */
    flash_new();
    memset(s_ram.mem, 0x5A, sizeof(s_ram.mem));
    CHECK(sh_hist_log_open(&log, &s_flash));
    CHECK_EQ(log.count, 0);
    CHECK_EQ(log.head, 0);
    for (unsigned s = 0; s < SECTORS; s++) {
        CHECK_EQ(s_ram.erases[s], 1);
    }
    append_days(&log, 1, 3);
    check_days(&log, 1, 3);

    /* the flash does not answer: no history, no crash */
    s_ram.fail_all = true;
    CHECK(!sh_hist_log_open(&again, &s_flash));
    CHECK(!sh_hist_log_append(&log, &d));
    CHECK(!sh_hist_log_get(&log, 0, &d));
    CHECK(!sh_hist_log_clear(&log));
    s_ram.fail_all = false;

    /* a partition too small for a ring */
    const sh_hist_flash_t small = { .ctx = &s_ram, .size = SH_HIST_SECTOR, .read = ram_read,
                                    .write = ram_write, .erase = ram_erase };
    CHECK(!sh_hist_log_open(&log, &small));
    CHECK(!sh_hist_log_append(&log, &d));
}

/* ---- day chart and day ------------------------------------------------------------------ */

#define HMS(h, m, s)    ((uint32_t)((h) * 3600 + (m) * 60 + (s)))
#define DAY0            2500u                           /* the first local day of the tests */

typedef struct {
    sh_hist_t h;
    sh_hist_in_t in;
    uint32_t local;             /* local seconds since the start of DAY0 */
    int closed;                 /* days ended so far */
    sh_hist_day_t day;          /* the last one */
} sim_t;

static void sim_start(sim_t *s, uint32_t local, bool clock_set)
{
    memset(s, 0, sizeof(*s));
    s->in.now_s = 5;                                    /* the first step, a few seconds after boot */
    s->in.t_col = 200;
    s->in.t_tank = 400;
    s->in.clock_set = clock_set;
    s->in.zone_confirmed = true;
    s->local = local;
}

static void sim_step(sim_t *s)
{
    s->in.day = (uint16_t)(DAY0 + s->local / SH_HIST_DAY_S);
    s->in.sec = s->local % SH_HIST_DAY_S;
    sh_hist_day_t d;
    if (sh_hist_step(&s->h, &s->in, &d)) {
        s->closed++;
        s->day = d;
    }
}

/* Seconds go by, a step in each. */
static void sim_run(sim_t *s, uint32_t seconds)
{
    while (seconds--) {
        s->in.now_s++;
        s->local++;
        sim_step(s);
    }
}

/* Until the local time of the day is `sec` (the next time it is). */
static void sim_until(sim_t *s, uint32_t sec)
{
    sim_run(s, (sec + SH_HIST_DAY_S - s->local % SH_HIST_DAY_S - 1) % SH_HIST_DAY_S + 1);
}

static void sim_pump(sim_t *s, bool on, sh_pump_reason_t reason)
{
    s->in.pump_on = on;
    s->in.reason = reason;
    sim_step(s);                                        /* the control decides, then the history */
}

static sh_hist_point_t s_pts[SH_HIST_SLOTS];

/* Without the clock: intervals of 10 min since the start, the last 24 h. */
static void test_chart_no_clock(void)
{
    sim_t s;
    bool day = true;
    uint32_t run[SH_RUN_KINDS];
    sim_start(&s, 0, false);
    sim_step(&s);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 0);
    CHECK(!day);

    sim_run(&s, 599);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 0);
    s.in.t_col = 215;                                   /* the values at the end of the interval */
    s.in.t_tank = 398;
    sim_run(&s, 1);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 1);
    CHECK_EQ(s_pts[0].x, 0);
    CHECK_EQ(s_pts[0].t_col, 215);
    CHECK_EQ(s_pts[0].t_tank, 398);
    CHECK_EQ(s_pts[0].run_s[SH_RUN_CHARGE] + s_pts[0].run_s[SH_RUN_PROTECT] + s_pts[0].run_s[SH_RUN_OTHER], 0);

    /* the pump: 4 min charging, 1 min protection, 30 s test in the second interval */
    sim_run(&s, 60);
    sim_pump(&s, true, SH_PUMP_REASON_DIFF);
    sim_run(&s, 200);
    sim_pump(&s, true, SH_PUMP_REASON_MIN_RUN);         /* still charging */
    sim_run(&s, 40);
    sim_pump(&s, true, SH_PUMP_REASON_COL_PROTECT);
    sim_run(&s, 60);
    sim_pump(&s, false, SH_PUMP_REASON_TANK_MAX);
    sim_run(&s, 100);
    sim_pump(&s, true, SH_PUMP_REASON_TEST);
    sim_run(&s, 30);
    sim_pump(&s, false, SH_PUMP_REASON_DIFF);
    sh_hist_run_today(&s.h, run);                       /* the open interval counts too */
    CHECK(run[SH_RUN_CHARGE] == 240 && run[SH_RUN_PROTECT] == 60 && run[SH_RUN_OTHER] == 30);
    s.in.col_fault = true;                              /* a faulty sensor at the end: no value */
    sim_run(&s, 110);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 2);
    CHECK_EQ(s_pts[0].x, 10);                           /* minutes ago */
    CHECK_EQ(s_pts[1].x, 0);
    CHECK_EQ(s_pts[1].t_col, SH_TEMP_INVALID);
    CHECK_EQ(s_pts[1].t_tank, 398);
    CHECK_EQ(s_pts[1].run_s[SH_RUN_CHARGE], 240);
    CHECK_EQ(s_pts[1].run_s[SH_RUN_PROTECT], 60);
    CHECK_EQ(s_pts[1].run_s[SH_RUN_OTHER], 30);
    s.in.col_fault = false;

    /* a run over several intervals */
    sim_pump(&s, true, SH_PUMP_REASON_DIFF);
    sim_run(&s, 1500);
    sim_pump(&s, false, SH_PUMP_REASON_DIFF);
    sim_run(&s, 300);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 5);
    CHECK_EQ(s_pts[2].run_s[SH_RUN_CHARGE], 600);
    CHECK_EQ(s_pts[3].run_s[SH_RUN_CHARGE], 600);
    CHECK_EQ(s_pts[4].run_s[SH_RUN_CHARGE], 300);
    CHECK_EQ(s_pts[4].x, 0);
    CHECK_EQ(s_pts[0].x, 40);
    sh_hist_run_today(&s.h, run);
    CHECK(run[SH_RUN_CHARGE] == 1740 && run[SH_RUN_PROTECT] == 60 && run[SH_RUN_OTHER] == 30);

    /* 24 h after the start: 144 intervals; later the oldest ones drop out, with their pump time */
    sim_run(&s, SH_HIST_DAY_S - 3000);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 144);
    CHECK_EQ(s_pts[0].x, 1430);
    CHECK_EQ(s_pts[143].x, 0);
    sim_run(&s, 600);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 144);
    CHECK_EQ(s_pts[0].run_s[SH_RUN_CHARGE], 240);       /* the second interval is the oldest now */
    sh_hist_run_today(&s.h, run);
    CHECK(run[SH_RUN_CHARGE] == 1740 && run[SH_RUN_OTHER] == 30);
    sim_run(&s, 600);
    sh_hist_run_today(&s.h, run);
    CHECK(run[SH_RUN_CHARGE] == 1500 && run[SH_RUN_PROTECT] == 0 && run[SH_RUN_OTHER] == 0);
    sim_run(&s, 1800);
    sh_hist_run_today(&s.h, run);
    CHECK_EQ(run[SH_RUN_CHARGE], 0);
    CHECK(!day);
    CHECK_EQ(s.closed, 0);                              /* no clock: no day is stored */
    sim_run(&s, 3 * SH_HIST_DAY_S);
    CHECK_EQ(s.closed, 0);
    CHECK(!sh_hist_reliable(&s.h));
}

/* With the clock: the intervals end on the 10 min marks, the chart is of the local day. */
static void test_chart_clock(void)
{
    sim_t s;
    bool day = false;
    uint32_t run[SH_RUN_KINDS];
    sim_start(&s, HMS(8, 3, 20), true);
    sim_step(&s);
    CHECK(sh_hist_reliable(&s.h));                      /* set, the zone confirmed */
    sim_pump(&s, true, SH_PUMP_REASON_DIFF);
    sim_until(&s, HMS(8, 9, 59));
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 0);
    CHECK(day);
    sim_run(&s, 1);                                     /* 08:10:00 */
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 1);
    CHECK_EQ(s_pts[0].x, 8 * 60 + 10);                  /* minute of the day */
    CHECK_EQ(s_pts[0].run_s[SH_RUN_CHARGE], 400);
    sim_until(&s, HMS(8, 25, 0));
    sim_pump(&s, false, SH_PUMP_REASON_DIFF);
    sim_until(&s, HMS(23, 59, 59));
    const int n = sh_hist_chart(&s.h, s_pts, &day);
    CHECK_EQ(n, 95);                                    /* 08:10 ... 23:50 */
    CHECK_EQ(s_pts[1].x, 8 * 60 + 20);
    CHECK_EQ(s_pts[2].run_s[SH_RUN_CHARGE], 300);
    CHECK_EQ(s_pts[n - 1].x, 23 * 60 + 50);
    sh_hist_run_today(&s.h, run);
    CHECK_EQ(run[SH_RUN_CHARGE], 1300);

    /* midnight: the chart and the pump time start again */
    sim_run(&s, 1);
    CHECK_EQ(s.closed, 1);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 0);
    sh_hist_run_today(&s.h, run);
    CHECK_EQ(run[SH_RUN_CHARGE], 0);
    sim_until(&s, HMS(0, 10, 0));
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 1);
    CHECK_EQ(s_pts[0].x, 10);
    sim_until(&s, HMS(23, 59, 59));
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 143);
    CHECK_EQ(s_pts[142].x, 1430);
    sim_until(&s, HMS(12, 0, 0));
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 72);
    CHECK_EQ(s_pts[71].x, 720);
}

/* The clock is set some time after boot (browser, AP mode): what was measured today stays. */
static void test_clock_set_later(void)
{
    sim_t s;
    bool day = false;
    uint32_t run[SH_RUN_KINDS];

    /* boot at 08:00, the clock at 09:05 */
    sim_start(&s, HMS(8, 0, 0), false);
    sim_step(&s);
    sim_pump(&s, true, SH_PUMP_REASON_DIFF);
    sim_run(&s, 900);
    sim_pump(&s, false, SH_PUMP_REASON_DIFF);
    s.in.t_col = 150;
    sim_until(&s, HMS(9, 5, 0));
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 6);
    CHECK(!day);
    s.in.clock_set = true;
    sim_run(&s, 1);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 6);
    CHECK(day);
    CHECK_EQ(s_pts[0].x, 8 * 60 + 10);
    CHECK_EQ(s_pts[5].x, 9 * 60);
    sh_hist_run_today(&s.h, run);
    CHECK_EQ(run[SH_RUN_CHARGE], 900);
    sim_until(&s, HMS(9, 10, 0));                       /* from now on, on the marks */
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 7);
    CHECK_EQ(s_pts[6].x, 9 * 60 + 10);
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 1);
    CHECK_EQ(s.day.day, DAY0);
    CHECK(s.day.partial);                               /* not observed from midnight */
    CHECK_EQ(s.day.run_min[SH_RUN_CHARGE], 15);
    CHECK_EQ(s.day.col_max, 200);
    CHECK_EQ(s.day.col_min, 150);
    CHECK_EQ(s.day.peak_min, 8 * 60);

    /* boot at 22:00, the clock at 00:30 the next day: only today counts */
    sim_start(&s, HMS(22, 0, 0), false);
    sim_step(&s);
    sim_pump(&s, true, SH_PUMP_REASON_TEST);
    s.in.t_col = 900;
    sim_until(&s, HMS(22, 5, 0));
    sim_pump(&s, false, SH_PUMP_REASON_DIFF);
    s.in.t_col = 100;
    sim_until(&s, HMS(0, 12, 0));
    sim_pump(&s, true, SH_PUMP_REASON_FROST);
    sim_until(&s, HMS(0, 15, 0));
    sim_pump(&s, false, SH_PUMP_REASON_DIFF);
    sim_until(&s, HMS(0, 30, 0));
    s.in.clock_set = true;
    sim_run(&s, 1);
    CHECK_EQ(s.closed, 0);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 3);      /* 00:10, 00:20, 00:30 */
    CHECK_EQ(s_pts[0].x, 10);
    sh_hist_run_today(&s.h, run);
    CHECK(run[SH_RUN_OTHER] == 0 && run[SH_RUN_PROTECT] == 180);
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 1);
    CHECK_EQ(s.day.day, DAY0 + 1);
    CHECK(s.day.partial);
    CHECK_EQ(s.day.col_max, 100);                       /* not the 90 °C of yesterday */
    CHECK_EQ(s.day.run_min[SH_RUN_PROTECT], 3);
    CHECK_EQ(s.day.run_min[SH_RUN_OTHER], 0);
}

/* A sunny day: the collector follows the sun, the highest value at `peak` (seconds of the day). */
static void sim_sunny_day(sim_t *s, uint32_t peak)
{
    s->in.t_col = 50;
    if (peak > 0) {
        sim_until(s, peak - 1);
    }
    s->in.t_col = 700;
    sim_run(s, 1);
    if (peak < HMS(23, 59, 59)) {
        s->in.t_col = 699;                              /* a lower value later is not the peak */
        sim_run(s, 1);
        s->in.t_col = 300;
        sim_until(s, HMS(23, 59, 59));
    }
    s->in.t_col = 60;
    sim_run(s, 1);                                      /* midnight */
}

/* The daily summary at local midnight. */
static void test_day_summary(void)
{
    sim_t s;
    sim_start(&s, HMS(23, 59, 0), true);
    sim_step(&s);
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 1);                              /* the minute after boot: a day too */
    CHECK(s.day.partial);
    CHECK_EQ(s.day.day, DAY0);

    /* a whole day */
    s.in.t_col = 31;
    s.in.t_tank = 402;
    sim_until(&s, HMS(5, 40, 0));
    s.in.t_col = -18;                                   /* the minimum, before sunrise */
    sim_run(&s, 600);
    s.in.t_col = 240;
    sim_until(&s, HMS(9, 0, 0));
    s.in.t_tank = 388;                                  /* the tank minimum: hot water used */
    sim_run(&s, 60);
    sim_pump(&s, true, SH_PUMP_REASON_DIFF);            /* 09:01:00 - 12:13:29: 192 min 29 s */
    s.in.t_col = 610;
    sim_until(&s, HMS(12, 13, 29));
    sim_pump(&s, true, SH_PUMP_REASON_COL_PROTECT);     /* 3 min 30 s -> 4 min */
    s.in.alarms = SH_ALARM_BIT(SH_ALARM_HOT);
    s.in.t_col = 1102;
    sim_run(&s, 210);
    sim_pump(&s, false, SH_PUMP_REASON_TANK_MAX);
    s.in.alarms = 0;
    s.in.t_col = 905;
    sim_until(&s, HMS(13, 10, 0));
    s.in.t_col = 1187;                                  /* the peak, at 13:10 */
    sim_run(&s, 30);
    s.in.t_col = 1187;                                  /* the same value later is not a new peak */
    sim_run(&s, 600);
    s.in.t_col = 800;
    s.in.t_tank = 715;                                  /* the tank maximum */
    sim_until(&s, HMS(15, 0, 0));
    s.in.t_tank = 702;
    s.in.col_fault = true;                              /* a faulty sensor: its value does not count */
    s.in.t_col = 1250;
    s.in.alarms = SH_ALARM_BIT(SH_ALARM_E1);
    sim_run(&s, 300);
    sim_pump(&s, true, SH_PUMP_REASON_CF_PROBE);        /* 89 s -> 1 min */
    sim_run(&s, 89);
    sim_pump(&s, false, SH_PUMP_REASON_CF_WAIT);
    s.in.col_fault = false;
    s.in.alarms = 0;
    s.in.t_col = 400;
    sim_until(&s, HMS(23, 59, 59));
    CHECK_EQ(s.closed, 1);
    s.in.col_fault = true;                              /* the next day: no sensor answers */
    s.in.tank_fault = true;
    sim_run(&s, 1);
    CHECK_EQ(s.closed, 2);
    CHECK_EQ(s.day.day, DAY0 + 1);
    CHECK(!s.day.partial);
    CHECK_EQ(s.day.col_max, 1187);
    CHECK_EQ(s.day.col_min, -18);
    CHECK_EQ(s.day.tank_max, 715);
    CHECK_EQ(s.day.tank_min, 388);
    CHECK_EQ(s.day.run_min[SH_RUN_CHARGE], 192);
    CHECK_EQ(s.day.run_min[SH_RUN_PROTECT], 4);
    CHECK_EQ(s.day.run_min[SH_RUN_OTHER], 1);
    CHECK_EQ(s.day.cool_min, 0);                        /* groups C / D */
    CHECK_EQ(s.day.heater_min, 0);
    CHECK_EQ(s.day.alarms, SH_ALARM_BIT(SH_ALARM_HOT) | SH_ALARM_BIT(SH_ALARM_E1));
    CHECK_EQ(s.day.peak_min, 13 * 60 + 10);
    CHECK(s.day.reliable);
    CHECK_EQ(s.day.sunrise, SH_HIST_NONE16);
    CHECK_EQ(s.day.sunset, SH_HIST_NONE16);
    CHECK_EQ(s.day.cool_target, SH_HIST_NONE8);
    CHECK_EQ(s.h.peak.last_min, 13 * 60 + 10);
    CHECK(s.h.peak.last_in);

    /* the next day starts from nothing; no collector value at all -> no peak */
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 3);
    CHECK_EQ(s.day.day, DAY0 + 2);
    CHECK_EQ(s.day.col_max, SH_TEMP_INVALID);
    CHECK_EQ(s.day.col_min, SH_TEMP_INVALID);
    CHECK_EQ(s.day.tank_max, SH_TEMP_INVALID);
    CHECK_EQ(s.day.peak_min, SH_PEAK_NONE);
    CHECK_EQ(s.day.alarms, 0);
    CHECK_EQ(s.day.run_min[SH_RUN_CHARGE] + s.day.run_min[SH_RUN_PROTECT] + s.day.run_min[SH_RUN_OTHER], 0);
    CHECK_EQ(s.h.peak.last_min, 13 * 60 + 10);          /* the check keeps the last sunny day */

    /* a pump that runs over midnight: each day gets its part */
    s.in.col_fault = false;
    s.in.tank_fault = false;
    sim_until(&s, HMS(23, 50, 0));
    sim_pump(&s, true, SH_PUMP_REASON_FROST);
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.day.run_min[SH_RUN_PROTECT], 10);
    sim_until(&s, HMS(0, 20, 0));
    sim_pump(&s, false, SH_PUMP_REASON_DIFF);
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.day.run_min[SH_RUN_PROTECT], 20);
    CHECK_EQ(s.closed, 5);
}

/* The time zone check on whole days: the window to the minute, the rows, the cloudy days. */
static void test_day_peak_check(void)
{
    sim_t s;
    sim_start(&s, HMS(23, 0, 0), true);
    sim_step(&s);
    sim_until(&s, HMS(0, 0, 0));
    CHECK(sh_hist_reliable(&s.h));

    /* the peak of a whole day, to the minute */
    const struct {
        uint32_t sec;
        bool in;
    } t[] = {
        { HMS(9, 59, 59), false }, { HMS(10, 0, 0), true }, { HMS(17, 0, 59), true },
        { HMS(17, 1, 0), false }, { HMS(0, 0, 0), false }, { HMS(23, 59, 59), false },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        sh_hist_peak_reset(&s.h, true);
        sim_sunny_day(&s, t[i].sec);
        CHECK(!s.day.partial);
        CHECK_EQ(s.day.peak_min, t[i].sec / 60);
        CHECK_EQ(s.h.peak.last_min, t[i].sec / 60);
        CHECK_EQ(s.h.peak.last_in, t[i].in);
        CHECK(s.day.reliable);
    }

    /* two sunny days in a row with the peak in the evening: unreliable, stored with the day */
    sh_hist_peak_reset(&s.h, true);
    sim_sunny_day(&s, HMS(20, 30, 0));
    CHECK(sh_hist_reliable(&s.h));
    CHECK(s.day.reliable);
    s.in.t_col = 50;                                    /* a cloudy day between them */
    sim_until(&s, HMS(21, 0, 0));
    s.in.t_col = 249;
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.day.peak_min, 21 * 60);
    CHECK(sh_hist_reliable(&s.h));
    sim_sunny_day(&s, HMS(20, 40, 0));
    CHECK(!sh_hist_reliable(&s.h));
    CHECK(!s.day.reliable);
    CHECK(!s.h.peak.last_in);

    /* two in the window: reliable again */
    sim_sunny_day(&s, HMS(13, 0, 0));
    CHECK(!sh_hist_reliable(&s.h));
    sim_sunny_day(&s, HMS(14, 0, 0));
    CHECK(sh_hist_reliable(&s.h));
    CHECK(s.day.reliable);

    /* a zone that is not confirmed: unreliable until two days in the window */
    sh_hist_peak_reset(&s.h, false);
    CHECK(!sh_hist_reliable(&s.h));
    sim_sunny_day(&s, HMS(12, 0, 0));
    CHECK(!s.day.reliable);
    sim_sunny_day(&s, HMS(12, 0, 0));
    CHECK(s.day.reliable);

    /* a day that is not whole does not count */
    sim_start(&s, HMS(18, 0, 0), true);
    s.in.t_col = 900;                                   /* the "peak" is just the boot */
    sim_step(&s);
    s.in.t_col = 100;
    sim_until(&s, HMS(0, 0, 0));
    CHECK(s.day.partial);
    CHECK_EQ(s.day.peak_min, 18 * 60);
    CHECK_EQ(s.h.peak.out_row, 0);
    CHECK_EQ(s.h.peak.last_min, SH_PEAK_NONE);
    sim_sunny_day(&s, HMS(18, 0, 0));
    CHECK_EQ(s.h.peak.out_row, 1);
}

/* The clock changes: corrections, daylight saving time, a new clock. */
static void test_clock_changes(void)
{
    sim_t s;
    bool day = false;

    /* SNTP corrects a few seconds: nothing happens */
    sim_start(&s, HMS(11, 0, 0), true);
    sim_step(&s);
    sim_until(&s, HMS(12, 0, 0));
    s.local += 40;
    sim_run(&s, 100);
    s.local -= 75;
    sim_run(&s, 100);
    CHECK(sh_hist_reliable(&s.h));
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 1);
    CHECK_EQ(s.day.day, DAY0);

    /* ... also back over midnight: the day is stored once */
    sim_run(&s, 3);
    s.local -= 10;                                      /* 23:59:53 again */
    sim_run(&s, 30);
    CHECK_EQ(s.closed, 1);
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 2);
    CHECK_EQ(s.day.day, DAY0 + 1);
    CHECK(!s.day.partial);

    /* daylight saving time: an hour forward at 03:00, the day goes on and is a whole one */
    sim_until(&s, HMS(3, 0, 0));
    s.local += 3600;
    sim_run(&s, 1);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 19);     /* 00:10 ... 03:00, then 04:00 */
    CHECK_EQ(s_pts[0].x, 10);
    CHECK_EQ(s_pts[17].x, 3 * 60);
    CHECK_EQ(s_pts[18].x, 4 * 60);
    sim_sunny_day(&s, HMS(14, 0, 0));
    CHECK_EQ(s.closed, 3);
    CHECK(!s.day.partial);
    CHECK_EQ(s.day.peak_min, 14 * 60);
    CHECK_EQ(s.h.peak.in_row, 1);
    /* ... and an hour back at 04:00 */
    sim_until(&s, HMS(4, 0, 0));
    s.local -= 3600;
    sim_run(&s, 1);
    sim_sunny_day(&s, HMS(14, 0, 0));
    CHECK_EQ(s.closed, 4);
    CHECK(!s.day.partial);
    CHECK_EQ(s.day.peak_min, 14 * 60);
    CHECK_EQ(s.h.peak.in_row, 2);

    /* a new clock (the browser set a wrong one, SNTP corrects it by days): the old day is not
     * stored, the new one is not whole, the time zone check starts again */
    sim_sunny_day(&s, HMS(20, 0, 0));
    sim_sunny_day(&s, HMS(20, 0, 0));
    CHECK(!sh_hist_reliable(&s.h));
    CHECK_EQ(s.closed, 6);
    sim_until(&s, HMS(10, 0, 0));
    s.local += 5 * SH_HIST_DAY_S + 7200;
    sim_run(&s, 1);
    CHECK_EQ(s.closed, 6);
    CHECK(sh_hist_reliable(&s.h));
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 7);
    CHECK(s.day.partial);
    CHECK_EQ(s.day.day, DAY0 + 6 + 5);

    /* a new zone moves the clock by hours: the same */
    sim_until(&s, HMS(10, 0, 0));
    s.local -= 7 * 3600;
    sim_run(&s, 1);
    CHECK_EQ(sh_hist_chart(&s.h, s_pts, &day), 19);     /* the last 3 h, at their new local time */
    CHECK_EQ(s_pts[0].x, 10);
    CHECK_EQ(s_pts[17].x, 3 * 60);
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 8);
    CHECK(s.day.partial);
    sim_until(&s, HMS(0, 0, 0));
    CHECK_EQ(s.closed, 9);
    CHECK(!s.day.partial);
}

int main(void)
{
    test_run_kind();
    test_record();
    test_csv();
    test_log_basic();
    test_log_ring();
    test_log_damage();
    test_log_power_loss();
    test_chart_no_clock();
    test_chart_clock();
    test_clock_set_later();
    test_day_summary();
    test_day_peak_check();
    test_clock_changes();
    return test_summary("test_history");
}
