/* Web interface: status and request checks — see sh_web_api.h. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarm.h"
#include "sh_web_api.h"

/* The functions this firmware has. A step that adds a function adds its name here and wires its
 * elements of the page (main/web/index.html, data-f="name"). */
static const char *const FEATURES[] = {
    "alarms", "temps", "pump", "pump_test", "settings", "frost",
    "fluid", "pressure", "col_max", "calc", "net", "fw", "reset",
    "clock", "timezone", "run_today", "chart", "learned",
};
#define FEATURE_HISTORY "history"       /* only with the "history" partition */

/* ---- JSON ------------------------------------------------------------------------------- */

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

/* A quoted string; quotes, backslashes and control characters are escaped (UTF-8 passes). */
static void put_str(out_t *o, const char *s)
{
    put(o, "\"");
    for (; s && *s; s++) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            put(o, "\\%c", c);
        } else if (c < 0x20) {
            put(o, "\\u%04x", c);
        } else {
            put(o, "%c", c);
        }
    }
    put(o, "\"");
}

static void put_temp(out_t *o, const char *name, int16_t t, bool fault)
{
    if (fault || t == SH_TEMP_INVALID) {
        put(o, "\"%s\":null,", name);
    } else {
        put(o, "\"%s\":%d,", name, t);
    }
}

int sh_web_status_json(char *buf, size_t size, const sh_web_status_t *s)
{
    out_t o = { .buf = buf, .size = size };
    if (size == 0) {
        return -1;
    }

    put(&o, "{\"name\":");
    put_str(&o, s->name);
    put(&o, ",\"fw\":");
    put_str(&o, s->version);
    put(&o, ",\"feat\":[");
    for (size_t i = 0; i < sizeof(FEATURES) / sizeof(FEATURES[0]); i++) {
        put(&o, "%s\"%s\"", i ? "," : "", FEATURES[i]);
    }
    put(&o, "%s],", s->history ? ",\"" FEATURE_HISTORY "\"" : "");

    put_temp(&o, "tank", s->t_tank, s->tank_fault);
    put_temp(&o, "col", s->t_col, s->col_fault);
    put(&o, "\"pump\":%d,\"why\":\"%s\",", s->pump_on, sh_web_reason_key(s->reason));
    put(&o, "\"alarms\":[");
    bool first = true;
    for (int a = 0; a < SH_ALARM_COUNT; a++) {      /* the alarms by number, then the information */
        if (s->alarms & SH_ALARM_BIT(a)) {
            put(&o, "%s\"%s\"", first ? "" : ",", sh_alarm_name((sh_alarm_t)a));
            first = false;
        }
    }
    put(&o, "],\"test\":%d,\"left\":%lu,", s->test_on, (unsigned long)s->test_left_s);
    put(&o, "\"run\":[%lu,%lu,%lu],", (unsigned long)s->run_s[SH_RUN_CHARGE],
        (unsigned long)s->run_s[SH_RUN_PROTECT], (unsigned long)s->run_s[SH_RUN_OTHER]);
    put(&o, "\"clock\":{\"set\":%d,\"ok\":%d,\"min\":%u},\"tz\":%d,", s->clock_set, s->clock_ok,
        s->clock_min, s->cfg.tz);
    if (s->peak_min == SH_PEAK_NONE) {
        put(&o, "\"peak\":null,");
    } else {
        put(&o, "\"peak\":{\"min\":%u,\"in\":%d},", s->peak_min, s->peak_in);
    }

    /* the menu values with the range the others leave them */
    put(&o, "\"set\":{");
    for (int id = 0; id < SH_SET_COUNT; id++) {
        int16_t min, max;
        sh_settings_range(&s->cfg.settings, (sh_setting_t)id, &min, &max);
        put(&o, "%s\"%s\":{\"v\":%d,\"min\":%d,\"max\":%d,\"step\":%d}", id ? "," : "",
            sh_setting_info[id].key, s->cfg.settings.v[id], min, max, sh_setting_info[id].step);
    }
    put(&o, "},\"fro\":%d,", s->cfg.frost);

    put(&o, "\"inst\":{");
    for (int id = 0; id < SH_INST_COUNT; id++) {
        put(&o, "%s\"%s\":{\"v\":%d,\"min\":%d,\"max\":%d}", id ? "," : "", sh_inst_info[id].key,
            s->cfg.inst.v[id], sh_inst_info[id].min, sh_inst_info[id].max);
    }
    sh_limits_t lim;
    sh_limits_calc(&s->cfg.inst, &lim);
    const int16_t used = s->col_protect < lim.protect_cap ? s->col_protect : lim.protect_cap;
    put(&o, "},\"calc\":{\"boil\":%d,\"cap\":%d,\"prot\":%d,\"prot_set\":%d,\"frost\":%d},",
        lim.boiling, lim.protect_cap, used, s->col_protect, lim.frost);

    put(&o, "\"net\":{\"ssid\":");
    put_str(&o, s->ssid);
    put(&o, ",\"ip\":");
    put_str(&o, s->ip);
    put(&o, ",\"rssi\":%d}}", s->rssi);

    return o.full ? -1 : (int)o.len;
}

static void put_temp_item(out_t *o, int16_t t)
{
    if (t == SH_TEMP_INVALID) {
        put(o, "null,");
    } else {
        put(o, "%d,", t);
    }
}

int sh_web_history_json(char *buf, size_t size, const sh_hist_point_t *pts, int n, bool day,
                        const sh_hist_day_t *days, int n_days)
{
    out_t o = { .buf = buf, .size = size };
    if (size == 0) {
        return -1;
    }
    put(&o, "{\"day\":%d,\"pts\":[", day);
    for (int i = 0; i < n; i++) {
        put(&o, "%s[%d,", i ? "," : "", pts[i].x);
        put_temp_item(&o, pts[i].t_col);
        put_temp_item(&o, pts[i].t_tank);
        put(&o, "%u,%u,%u]", pts[i].run_s[SH_RUN_CHARGE], pts[i].run_s[SH_RUN_PROTECT],
            pts[i].run_s[SH_RUN_OTHER]);
    }
    put(&o, "],\"days\":[");
    for (int i = 0; i < n_days; i++) {
        const sh_hist_day_t *d = &days[i];
        int y, m, md;
        sh_clock_date(d->day, &y, &m, &md);
        put(&o, "%s{\"d\":\"%04d-%02d-%02d\",\"t\":[", i ? "," : "", y, m, md);
        put_temp_item(&o, d->col_max);
        put_temp_item(&o, d->col_min);
        put_temp_item(&o, d->tank_max);
        if (d->tank_min == SH_TEMP_INVALID) {
            put(&o, "null");
        } else {
            put(&o, "%d", d->tank_min);
        }
        put(&o, "],\"run\":[%u,%u,%u],\"al\":[", d->run_min[SH_RUN_CHARGE], d->run_min[SH_RUN_PROTECT],
            d->run_min[SH_RUN_OTHER]);
        bool first = true;
        for (int a = 0; a < SH_ALARM_COUNT; a++) {
            if (d->alarms & SH_ALARM_BIT(a)) {
                put(&o, "%s\"%s\"", first ? "" : ",", sh_alarm_name((sh_alarm_t)a));
                first = false;
            }
        }
        put(&o, "]}");
    }
    put(&o, "]}");
    return o.full ? -1 : (int)o.len;
}

int sh_web_zones_json(char *buf, size_t size)
{
    out_t o = { .buf = buf, .size = size };
    if (size == 0) {
        return -1;
    }
    put(&o, "[");
    for (int i = 0; i < sh_zone_count; i++) {
        put(&o, "%s\"%s\"", i ? "," : "", sh_zones[i].name);
    }
    put(&o, "]");
    return o.full ? -1 : (int)o.len;
}

/* ---- requests --------------------------------------------------------------------------- */

#define VALUE_MAX       16      /* longest value with its NUL */

/* The next "key=value" of a form; false at the end or on a malformed / too long pair (then
 * `*body` is NULL). Values are plain words and numbers, so nothing is decoded: a '%' or '+' is
 * malformed. */
static bool next_pair(const char **body, char *key, char *value)
{
    const char *p = *body;
    if (p == NULL || *p == '\0') {
        return false;
    }
    char *dst = key;
    size_t n = 0;
    bool in_value = false;
    for (;; p++) {
        const char c = *p;
        if (c == '=' && !in_value) {
            *dst = '\0';
            dst = value;
            n = 0;
            in_value = true;
        } else if (c == '&' || c == '\0') {
            *dst = '\0';
            if (!in_value || key[0] == '\0') {
                break;                      /* malformed */
            }
            *body = c ? p + 1 : p;
            return true;
        } else if (c == '%' || c == '+' || n >= VALUE_MAX - 1) {
            break;
        } else {
            *dst++ = c;
            n++;
        }
    }
    *body = NULL;
    return false;
}

/* The value of `key` in a form (the first one); false if missing or the form is malformed. */
static bool form_get(const char *body, const char *key, char *value)
{
    char k[VALUE_MAX];
    while (next_pair(&body, k, value)) {
        if (strcmp(k, key) == 0) {
            return true;
        }
    }
    return false;
}

/* A whole decimal number that fits int16_t, nothing else. */
static bool parse_int(const char *s, int16_t *out)
{
    char *end;
    if (!(*s == '-' || (*s >= '0' && *s <= '9'))) {
        return false;                       /* strtol() would take spaces and a '+' */
    }
    const long v = strtol(s, &end, 10);
    if (*end != '\0' || v < INT16_MIN || v > INT16_MAX) {
        return false;
    }
    *out = (int16_t)v;
    return true;
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

bool sh_web_code_ok(const char *body, const char *code)
{
    char v[VALUE_MAX];
    if (code == NULL || strlen(code) != SH_WEB_CODE_LEN || !form_get(body, "code", v) ||
        strlen(v) != SH_WEB_CODE_LEN) {
        return false;
    }
    for (int i = 0; i < SH_WEB_CODE_LEN; i++) {
        if (upper(v[i]) != upper(code[i])) {
            return false;
        }
    }
    return true;
}

sh_web_res_t sh_web_set(const char *body, const char *code, sh_web_config_t *cfg, unsigned *changed)
{
    sh_web_config_t c = *cfg;
    unsigned ch = 0;
    bool glycol_given = false;
    const char *p = body;
    char key[VALUE_MAX], value[VALUE_MAX];

    while (next_pair(&p, key, value)) {
        if (strcmp(key, "code") == 0) {
            continue;
        }
        int16_t v;
        if (!parse_int(value, &v)) {
            return SH_WEB_BAD;
        }
        bool known = false;
        for (int id = 0; id < SH_SET_COUNT && !known; id++) {
            if (strcmp(key, sh_setting_info[id].key) == 0) {
                int16_t min, max;
                sh_settings_range(&c.settings, (sh_setting_t)id, &min, &max);
                if (!sh_settings_value_ok((sh_setting_t)id, v) || v < min || v > max) {
                    return SH_WEB_BAD;
                }
                c.settings.v[id] = v;
                ch |= SH_WEB_CH_SETTINGS;
                known = true;
            }
        }
        if (!known && strcmp(key, "fro") == 0) {
            if (v != 0 && v != 1) {
                return SH_WEB_BAD;
            }
            c.frost = v != 0;
            ch |= SH_WEB_CH_FROST;
            known = true;
        }
        for (int id = 0; id < SH_INST_COUNT && !known; id++) {
            if (strcmp(key, sh_inst_info[id].key) == 0) {
                c.inst.v[id] = v;
                glycol_given = glycol_given || id == SH_INST_GLYCOL;
                ch |= SH_WEB_CH_INSTALLER;
                known = true;
            }
        }
        if (!known && strcmp(key, "tz") == 0) {
            if (v < 0 || v >= sh_zone_count) {
                return SH_WEB_BAD;
            }
            c.tz = v;               /* also the zone in use: this confirms it */
            ch |= SH_WEB_CH_TZ;
            known = true;
        }
        if (!known) {
            return SH_WEB_BAD;
        }
    }
    if (p == NULL || ch == 0) {
        return SH_WEB_BAD;                  /* malformed form, or nothing to set */
    }
    if ((ch & (SH_WEB_CH_INSTALLER | SH_WEB_CH_TZ)) && !sh_web_code_ok(body, code)) {
        return SH_WEB_DENIED;
    }
    if (ch & SH_WEB_CH_INSTALLER) {
        if (c.inst.v[SH_INST_FLUID] == SH_FLUID_WATER && !glycol_given) {
            c.inst.v[SH_INST_GLYCOL] = 0;   /* water has no glycol */
        }
        if (!sh_installer_ok(&c.inst)) {
            return SH_WEB_BAD;
        }
    }
    *cfg = c;
    *changed = ch;
    return SH_WEB_OK;
}

sh_web_res_t sh_web_test(const char *body, bool test_on, sh_web_test_t *cmd)
{
    char v[VALUE_MAX];
    if (!form_get(body, "cmd", v)) {
        return SH_WEB_BAD;
    }
    if (strcmp(v, "start") == 0) {
        *cmd = test_on ? SH_WEB_TEST_NONE : SH_WEB_TEST_PLUS;
    } else if (strcmp(v, "plus") == 0) {
        *cmd = test_on ? SH_WEB_TEST_PLUS : SH_WEB_TEST_NONE;
    } else if (strcmp(v, "minus") == 0) {
        *cmd = test_on ? SH_WEB_TEST_MINUS : SH_WEB_TEST_NONE;
    } else if (strcmp(v, "stop") == 0) {
        *cmd = test_on ? SH_WEB_TEST_STOP : SH_WEB_TEST_NONE;
    } else {
        return SH_WEB_BAD;
    }
    return SH_WEB_OK;
}

sh_web_res_t sh_web_reset(const char *body, const char *code, sh_web_reset_t *what)
{
    char v[VALUE_MAX];
    if (!form_get(body, "what", v)) {
        return SH_WEB_BAD;
    }
    if (strcmp(v, "wifi") == 0) {
        *what = SH_WEB_RESET_WIFI;
    } else if (strcmp(v, "learned") == 0) {
        *what = SH_WEB_RESET_LEARNED;
    } else if (strcmp(v, "history") == 0) {
        *what = SH_WEB_RESET_HISTORY;
    } else if (strcmp(v, "defaults") == 0) {
        *what = SH_WEB_RESET_DEFAULTS;
    } else if (strcmp(v, "factory") == 0) {
        *what = SH_WEB_RESET_FACTORY;
    } else {
        return SH_WEB_BAD;
    }
    return sh_web_code_ok(body, code) ? SH_WEB_OK : SH_WEB_DENIED;
}

#define TIME_MIN        1704067200L     /* 2024-01-01: an earlier browser clock is not a clock */

sh_web_res_t sh_web_time(const char *body, uint32_t *epoch_s)
{
    char v[VALUE_MAX];
    if (!form_get(body, "t", v) || v[0] < '0' || v[0] > '9') {
        return SH_WEB_BAD;
    }
    char *end;
    const long long t = strtoll(v, &end, 10);
    if (*end != '\0' || t < TIME_MIN || t > UINT32_MAX) {
        return SH_WEB_BAD;
    }
    *epoch_s = (uint32_t)t;
    return SH_WEB_OK;
}

const char *sh_web_reason_key(sh_pump_reason_t reason)
{
    static const char *const keys[SH_PUMP_REASON_COUNT] = {
        [SH_PUMP_REASON_NONE]         = "none",
        [SH_PUMP_REASON_SENSOR]       = "sensor",
        [SH_PUMP_REASON_COL_BOILING]  = "boiling",
        [SH_PUMP_REASON_TANK_CEILING] = "ceiling",
        [SH_PUMP_REASON_COL_PROTECT]  = "protect",
        [SH_PUMP_REASON_FROST]        = "frost",
        [SH_PUMP_REASON_TANK_MAX]     = "tmax",
        [SH_PUMP_REASON_DIFF]         = "diff",
        [SH_PUMP_REASON_MIN_RUN]      = "min_run",
        [SH_PUMP_REASON_MIN_OFF]      = "min_off",
        [SH_PUMP_REASON_COL_MIN]      = "col_min",
        [SH_PUMP_REASON_TEST]         = "test",
        [SH_PUMP_REASON_ANTI_SEIZE]   = "seize",
        [SH_PUMP_REASON_BOTH_FAULT]   = "both_fault",
        [SH_PUMP_REASON_TF_RUN]       = "tf_run",
        [SH_PUMP_REASON_TF_COLD]      = "tf_cold",
        [SH_PUMP_REASON_TF_HOT]       = "tf_hot",
        [SH_PUMP_REASON_TF_PROBE]     = "tf_probe",
        [SH_PUMP_REASON_CF_PROBE]     = "cf_probe",
        [SH_PUMP_REASON_CF_RUN]       = "cf_run",
        [SH_PUMP_REASON_CF_WAIT]      = "cf_wait",
        [SH_PUMP_REASON_CF_HEATER]    = "cf_heater",
    };
    if ((unsigned)reason < SH_PUMP_REASON_COUNT && keys[reason] != NULL) {
        return keys[reason];
    }
    return "none";
}
