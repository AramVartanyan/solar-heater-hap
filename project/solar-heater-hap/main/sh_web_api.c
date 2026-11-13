/* Web interface: status and request checks — see sh_web_api.h. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarm.h"
#include "sh_tz.h"
#include "sh_pump_ctl.h"        /* SH_AIR_* */
#include "sh_cool.h"
#include "sh_web_api.h"

/* The functions this firmware has. A step that adds a function adds its name here and wires its
 * elements of the page (main/web/index.html, data-f="name"). "history" is added when the history
 * partition is there. */
static const char *const FEATURES[] = {
    "alarms", "temps", "pump", "pump_test", "settings", "frost",
    "fluid", "pressure", "col_max", "calc", "net", "fw", "reset",
    "clock", "timezone", "run_today", "chart", "learned",
    "vacation", "season", "night_cool", "deaerate", "col_type", "tank_vol", "vac_limit",
    "heater", "mount",
};

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

static void put_or_null(out_t *o, bool have, int v)
{
    if (have) {
        put(o, "%d", v);
    } else {
        put(o, "null");
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
    put(&o, ",\"sn\":");
    put_str(&o, s->serial ? s->serial : "");
    put(&o, ",\"feat\":[");
    for (size_t i = 0; i < sizeof(FEATURES) / sizeof(FEATURES[0]); i++) {
        put(&o, "%s\"%s\"", i ? "," : "", FEATURES[i]);
    }
    if (s->history) {
        put(&o, ",\"history\"");
    }
    put(&o, "],");

    put_temp(&o, "tank", s->t_tank, s->tank_fault);
    put_temp(&o, "col", s->t_col, s->col_fault);
    put(&o, "\"pump\":%d,\"why\":\"%s\",", s->pump_on, sh_web_reason_key(s->reason));
    put(&o, "\"alarms\":[");
    bool first = true;
    for (int i = 0; i < SH_ALARM_COUNT; i++) {      /* the alarms by number, then the information */
        const sh_alarm_t a = sh_alarm_order[i];
        if (s->alarms & SH_ALARM_BIT(a)) {
            put(&o, "%s\"%s\"", first ? "" : ",", sh_alarm_name(a));
            first = false;
        }
    }
    put(&o, "],\"test\":%d,\"left\":%lu,", s->test_on, (unsigned long)s->test_left_s);

    /* season; night cooling: the session (1 tank above Tmax, 2 E4, 3 vacation), its target and
     * until when, the heater, the learned target and sunrise / sunset; air venting */
    put(&o, "\"winter\":%d,\"cool\":{\"on\":%d,\"target\":", s->winter, s->cool);
    put_or_null(&o, s->cool == SH_COOL_TMAX || s->cool == SH_COOL_E4, s->cool_target);
    put(&o, ",\"until\":");
    put_or_null(&o, s->cool != SH_COOL_NONE && s->cool_until >= 0, s->cool_until);
    put(&o, ",\"heater\":%d,\"learned\":%d,\"rise\":", s->cool_heater, s->cool_learned);
    put_or_null(&o, s->sunrise >= 0, s->sunrise);
    put(&o, ",\"set\":");
    put_or_null(&o, s->sunset >= 0, s->sunset);
    const uint32_t pause_s = sh_air_pause_s(&s->cfg.inst);
    put(&o, "},\"air\":{\"on\":%d,\"run\":%d,\"cycle\":%d,\"left\":%lu,\"cycles\":%u,\"work\":%u,"
        "\"pause\":%lu},", s->air_on, s->air_run, s->air_cycle, (unsigned long)s->air_left_s, SH_AIR_CYCLES,
        SH_AIR_RUN_S / 60u, (unsigned long)((pause_s + 30) / 60));

    /* the heater: the sign now, detected with "0 kW"; the leak (0 none, 1 check, 2 stopped), the
     * last check; the learned normal night cooling of the tank (E8) */
    put(&o, "\"heat\":{\"on\":%d,\"det\":%d,\"leak\":%d,\"left\":%lu,\"last\":", s->heater_on, s->heater_det,
        s->leak, (unsigned long)s->leak_left_s);
    if (s->leak_last) {
        put(&o, "{\"stop\":%d,\"drop\":%d}", s->leak_last_stop, s->leak_last_drop);
    } else {
        put(&o, "null");
    }
    put(&o, ",\"e8n\":");
    put_or_null(&o, s->e8_normal != INT16_MIN, s->e8_normal);
    put(&o, "},");

    /* the pulse mode: the age of the shown values (taken at a pulse / during a run / now), the
     * next pulse, no pulses after an overheated collector */
    put(&o, "\"pulse\":{\"on\":%d,\"age\":", s->pulse);
    put_or_null(&o, s->pulse && s->meas, (int)s->meas_age_s);
    put(&o, ",\"at\":%d,\"now\":%d,\"next\":", s->meas_pulse, s->meas_now);
    put_or_null(&o, s->pulse && s->pulse_next, (int)s->pulse_left_s);
    put(&o, ",\"hot\":%d},", s->pulse_hot);

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
        put(&o, "%s\"%s\":{\"v\":%d,\"min\":%d,\"max\":%d", id ? "," : "", sh_inst_info[id].key,
            s->cfg.inst.v[id], sh_inst_info[id].min, sh_inst_info[id].max);
        if (sh_inst_info[id].step != 1) {
            put(&o, ",\"step\":%d", sh_inst_info[id].step);
        }
        put(&o, "}");
    }
    sh_limits_t lim;
    sh_limits_calc(&s->cfg.inst, &lim);
    const int16_t used = s->col_protect < lim.protect_cap ? s->col_protect : lim.protect_cap;
    put(&o, "},\"calc\":{\"boil\":%d,\"cap\":%d,\"prot\":%d,\"prot_set\":%d,\"frost\":%d,\"vac\":%d},",
        lim.boiling, lim.protect_cap, used, s->col_protect, lim.frost, lim.vacation);

    put(&o, "\"net\":{\"ssid\":");
    put_str(&o, s->ssid);
    put(&o, ",\"ip\":");
    put_str(&o, s->ip);
    put(&o, ",\"rssi\":%d},", s->rssi);

    static const char *const trust[] = {
        [SH_CLOCK_UNSET] = "unset", [SH_CLOCK_TZ_UNCONFIRMED] = "tz",
        [SH_CLOCK_UNRELIABLE] = "bad", [SH_CLOCK_RELIABLE] = "ok",
    };
    const sh_web_clock_t *c = &s->clock;
    put(&o, "\"clock\":{\"st\":\"%s\",\"sntp\":%d,\"min\":", trust[c->trust], c->sntp);
    put_or_null(&o, c->trust != SH_CLOCK_UNSET && c->min >= 0, c->min);
    put(&o, ",\"tz\":");
    put_str(&o, c->tz);
    put(&o, ",\"tzok\":%d,\"peak\":", c->tz_confirmed);
    put_or_null(&o, c->peak >= 0, c->peak);
    put(&o, "},\"run\":[%lu,%lu,%lu,%lu]}", (unsigned long)((s->run_s[0] + 30) / 60),
        (unsigned long)((s->run_s[1] + 30) / 60), (unsigned long)((s->run_s[2] + 30) / 60),
        (unsigned long)((s->run_s[SH_RUN_COOL] + 30) / 60));

    return o.full ? -1 : (int)o.len;
}

/* ---- chart, days, time zones ------------------------------------------------------------ */

typedef struct {
    out_t *o;
    const sh_local_t *lt;
    uint32_t now_s;
    bool first;
} chart_ctx_t;

static void chart_slot(void *ctx, const sh_chart_slot_t *s)
{
    chart_ctx_t *c = ctx;
    const long x = c->lt->set ? (long)(s->idx + 1) * SH_CHART_SLOT_MIN : -(long)((c->now_s - s->end_s) / 60);
    put(c->o, "%s[%ld,", c->first ? "" : ",", x);
    put_or_null(c->o, s->t_col != SH_TEMP_INVALID, s->t_col);
    put(c->o, ",");
    put_or_null(c->o, s->t_tank != SH_TEMP_INVALID, s->t_tank);
    put(c->o, ",%u,%u,%u,%u]", (s->run_s[0] + 30u) / 60u, (s->run_s[1] + 30u) / 60u, (s->run_s[2] + 30u) / 60u,
        (s->run_s[SH_RUN_COOL] + 30u) / 60u);
    c->first = false;
}

int sh_web_chart_json(char *buf, size_t size, const sh_day_t *d, const sh_local_t *lt, uint32_t now_s)
{
    out_t o = { .buf = buf, .size = size };
    if (size == 0) {
        return -1;
    }
    put(&o, "{\"day\":%d,\"pts\":[", lt->set);
    chart_ctx_t c = { .o = &o, .lt = lt, .now_s = now_s, .first = true };
    sh_day_chart(d, lt, now_s, chart_slot, &c);
    put(&o, "]}");
    return o.full ? -1 : (int)o.len;
}

static void put_hist_temp(out_t *o, const char *name, int16_t t)
{
    put(o, ",\"%s\":", name);
    put_or_null(o, t != SH_HIST_NO_TEMP, t);
}

int sh_web_days_json(char *buf, size_t size, bool on, const sh_hist_rec_t *days, unsigned n)
{
    out_t o = { .buf = buf, .size = size };
    if (size == 0) {
        return -1;
    }
    put(&o, "{\"on\":%d,\"days\":[", on);
    for (unsigned i = 0; i < n; i++) {
        const sh_hist_rec_t *r = &days[i];
        int y, m, d;
        sh_day_date(r->day, &y, &m, &d);
        put(&o, "%s{\"d\":\"%04d-%02d-%02d\"", i ? "," : "", y, m, d);
        put_hist_temp(&o, "cx", r->col_max);
        put_hist_temp(&o, "cn", r->col_min);
        put_hist_temp(&o, "tx", r->tank_max);
        put_hist_temp(&o, "tn", r->tank_min);
        put(&o, ",\"run\":[%u,%u,%u],\"cool\":%u,\"heat\":%u,\"al\":[", r->run_min[0], r->run_min[1],
            r->run_min[2], r->cool_min, r->heater_min);
        bool first = true;
        for (int i = 0; i < SH_ALARM_COUNT; i++) {
            const sh_alarm_t a = sh_alarm_order[i];
            if (r->alarms & SH_ALARM_BIT(a)) {
                put(&o, "%s\"%s\"", first ? "" : ",", sh_alarm_name(a));
                first = false;
            }
        }
        put(&o, "],\"pk\":");
        put_or_null(&o, r->peak_min >= 0, r->peak_min);
        put(&o, ",\"rel\":%d,\"part\":%d}", r->clock_reliable, r->partial);
    }
    put(&o, "]}");
    return o.full ? -1 : (int)o.len;
}

int sh_web_tz_json(char *buf, size_t size)
{
    out_t o = { .buf = buf, .size = size };
    if (size == 0) {
        return -1;
    }
    put(&o, "{\"tz\":[");
    for (size_t i = 0; i < sh_tz_count; i++) {
        put(&o, "%s", i ? "," : "");
        put_str(&o, sh_tz_list[i].name);
    }
    put(&o, "]}");
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
        if (!known) {
            return SH_WEB_BAD;
        }
    }
    if (p == NULL || ch == 0) {
        return SH_WEB_BAD;                  /* malformed form, or nothing to set */
    }
    if (ch & SH_WEB_CH_INSTALLER) {
        if (!sh_web_code_ok(body, code)) {
            return SH_WEB_DENIED;
        }
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

sh_web_res_t sh_web_air(const char *body, bool air_on, bool test_on, sh_web_air_t *cmd)
{
    char v[VALUE_MAX];
    if (!form_get(body, "cmd", v)) {
        return SH_WEB_BAD;
    }
    if (strcmp(v, "start") == 0) {
        *cmd = air_on || test_on ? SH_WEB_AIR_NONE : SH_WEB_AIR_START;
    } else if (strcmp(v, "stop") == 0) {
        *cmd = air_on ? SH_WEB_AIR_STOP : SH_WEB_AIR_NONE;
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
    } else if (strcmp(v, "defaults") == 0) {
        *what = SH_WEB_RESET_DEFAULTS;
    } else if (strcmp(v, "factory") == 0) {
        *what = SH_WEB_RESET_FACTORY;
    } else if (strcmp(v, "history") == 0) {
        *what = SH_WEB_RESET_HISTORY;
    } else if (strcmp(v, "learned") == 0) {
        *what = SH_WEB_RESET_LEARNED;
    } else {
        return SH_WEB_BAD;
    }
    return sh_web_code_ok(body, code) ? SH_WEB_OK : SH_WEB_DENIED;
}

sh_web_res_t sh_web_time(const char *body, int64_t *epoch_s)
{
    char v[VALUE_MAX];
    if (!form_get(body, "t", v) || v[0] == '\0' || strlen(v) > 12) {
        return SH_WEB_BAD;
    }
    int64_t t = 0;
    for (const char *p = v; *p; p++) {
        if (*p < '0' || *p > '9') {
            return SH_WEB_BAD;
        }
        t = t * 10 + (*p - '0');
    }
    *epoch_s = t;
    return SH_WEB_OK;
}

sh_web_res_t sh_web_tz(const char *body, const char *code, int *tz)
{
    char v[VALUE_MAX];
    int16_t i;
    if (!form_get(body, "tz", v) || !parse_int(v, &i) || i < 0 || (size_t)i >= sh_tz_count) {
        return SH_WEB_BAD;
    }
    if (!sh_web_code_ok(body, code)) {
        return SH_WEB_DENIED;
    }
    *tz = i;
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
        [SH_PUMP_REASON_COOL]         = "cool",
        [SH_PUMP_REASON_VACATION]     = "vacation",
        [SH_PUMP_REASON_AIR_RUN]      = "air_run",
        [SH_PUMP_REASON_AIR_PAUSE]    = "air_pause",
        [SH_PUMP_REASON_LEAK_CHECK]   = "leak_check",
        [SH_PUMP_REASON_LEAK_STOP]    = "leak_stop",
        [SH_PUMP_REASON_PULSE]        = "pulse",
        [SH_PUMP_REASON_PULSE_WAIT]   = "pulse_wait",
    };
    if ((unsigned)reason < SH_PUMP_REASON_COUNT && keys[reason] != NULL) {
        return keys[reason];
    }
    return "none";
}
