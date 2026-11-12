/* Web interface data — see sh_web_data.h. */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "sh_state.h"           /* SH_TEMP_INVALID */
#include "sh_alarm.h"
#include "sh_web_data.h"

/* The names the page uses for its elements (data-f). A later step that adds a feature adds its
 * name here; the page then shows the element active. Not yet: vacation, season, night_cool, air,
 * run_today, chart, history, clock, timezone, city, ota, homekit, net_mode, heater, sensor_mount,
 * collector, tank_volume, vac_threshold, reset_homekit, reset_learned, reset_history. */
const char *const sh_web_features[] = {
    "alarms", "temps", "pump", "test", "settings", "frost", "installer", "calc", "network",
    "firmware", "reset_wifi", "reset_settings", "reset_factory", NULL,
};

/* ---- JSON writer ------------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t size;
    size_t len;
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
        return;
    }
    o->len += (size_t)n;
}

/* A JSON string: quotes, backslashes and control characters escaped; other bytes as they are. */
static void put_str(out_t *o, const char *s)
{
    put(o, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        if (*p == '"' || *p == '\\') {
            put(o, "\\%c", *p);
        } else if (*p < 0x20) {
            put(o, "\\u%04x", *p);
        } else {
            put(o, "%c", *p);
        }
    }
    put(o, "\"");
}

/* A temperature, or null when the sensor is faulty or has no value. */
static void put_temp(out_t *o, int16_t t, bool fault)
{
    if (fault || t == SH_TEMP_INVALID) {
        put(o, "null");
    } else {
        put(o, "%d", t);
    }
}

size_t sh_web_state_json(const sh_web_snap_t *s, char *buf, size_t size)
{
    if (size == 0) {
        return 0;
    }
    out_t o = { .buf = buf, .size = size };

    put(&o, "{\"f\":[");
    for (int i = 0; sh_web_features[i]; i++) {
        put(&o, i ? ",\"%s\"" : "\"%s\"", sh_web_features[i]);
    }
    put(&o, "],\"ver\":");
    put_str(&o, s->version);
    put(&o, ",\"host\":");
    put_str(&o, s->host);

    /* the codes in the order of the display: the alarms by number, then the information */
    put(&o, ",\"al\":[");
    bool first = true;
    for (int a = 0; a < SH_ALARM_COUNT; a++) {
        if (s->alarms & SH_ALARM_BIT(a)) {
            put(&o, first ? "\"%s\"" : ",\"%s\"", sh_alarm_name((sh_alarm_t)a));
            first = false;
        }
    }

    put(&o, "],\"tc\":");
    put_temp(&o, s->t_col, s->col_fault);
    put(&o, ",\"tt\":");
    put_temp(&o, s->t_tank, s->tank_fault);
    put(&o, ",\"cf\":%d,\"tf\":%d,\"pump\":%d,\"why\":\"%s\",\"test\":", s->col_fault, s->tank_fault,
        s->pump_on, sh_web_reason_key(s->reason));
    if (s->test_on) {
        put(&o, "%lu", (unsigned long)s->test_left_s);
    } else {
        put(&o, "null");
    }

    /* settings: the values and their ranges [min, max, step] (d.on / d.oF narrowed by each other) */
    put(&o, ",\"set\":{");
    for (int id = 0; id < SH_SET_COUNT; id++) {
        put(&o, "\"%s\":%d,", sh_setting_info[id].key, s->settings.v[id]);
    }
    put(&o, "\"frost\":%d},\"rng\":{", s->frost_on);
    for (int id = 0; id < SH_SET_COUNT; id++) {
        int16_t min, max;
        sh_settings_range(&s->settings, (sh_setting_t)id, &min, &max);
        put(&o, "%s\"%s\":[%d,%d,%d]", id ? "," : "", sh_setting_info[id].key, min, max,
            sh_setting_info[id].step);
    }
    for (int id = 0; id < SH_INST_COUNT; id++) {
        put(&o, ",\"%s\":[%d,%d,%d]", sh_inst_info[id].key, sh_inst_info[id].min,
            sh_inst_info[id].max, sh_inst_info[id].step);
    }

    put(&o, "},\"inst\":{");
    for (int id = 0; id < SH_INST_COUNT; id++) {
        put(&o, "%s\"%s\":%d", id ? "," : "", sh_inst_info[id].key, s->inst.v[id]);
    }
    put(&o, "},\"calc\":{\"boil\":%d,\"cap\":%d,\"prot\":%d,\"used\":%d,\"ban\":%d,\"frost\":%d}",
        s->limits.boiling, s->limits.protect_cap, s->col_protect, s->col_protect_used,
        s->limits.boiling, s->limits.frost);

    put(&o, ",\"net\":{\"ssid\":");
    put_str(&o, s->ssid);
    put(&o, ",\"ip\":");
    put_str(&o, s->ip);
    put(&o, ",\"rssi\":%d}}", s->rssi);

    return o.full ? 0 : o.len;
}

/* ---- requests --------------------------------------------------------------------------- */

bool sh_web_param_find(const char *key, sh_web_param_t *out)
{
    if (!key) {
        return false;
    }
    for (int id = 0; id < SH_SET_COUNT; id++) {
        if (strcmp(key, sh_setting_info[id].key) == 0) {
            *out = (sh_web_param_t){ .group = SH_WEB_PARAM_SETTING, .id = id, .code = false };
            return true;
        }
    }
    if (strcmp(key, "frost") == 0) {
        *out = (sh_web_param_t){ .group = SH_WEB_PARAM_FROST, .id = 0, .code = false };
        return true;
    }
    for (int id = 0; id < SH_INST_COUNT; id++) {
        if (strcmp(key, sh_inst_info[id].key) == 0) {
            *out = (sh_web_param_t){ .group = SH_WEB_PARAM_INSTALLER, .id = id, .code = true };
            return true;
        }
    }
    return false;
}

bool sh_web_cmd_find(const char *name, sh_web_cmd_t *cmd, bool *code)
{
    static const struct {
        const char *name;
        bool code;
    } cmds[SH_WEB_CMD_COUNT] = {
        [SH_WEB_CMD_TEST_PLUS]      = { "test_plus", false },
        [SH_WEB_CMD_TEST_MINUS]     = { "test_minus", false },
        [SH_WEB_CMD_TEST_STOP]      = { "test_stop", false },
        [SH_WEB_CMD_RESET_WIFI]     = { "reset_wifi", true },
        [SH_WEB_CMD_RESET_SETTINGS] = { "reset_settings", true },
        [SH_WEB_CMD_RESET_FACTORY]  = { "reset_factory", true },
    };
    if (!name) {
        return false;
    }
    for (int c = 0; c < SH_WEB_CMD_COUNT; c++) {
        if (strcmp(name, cmds[c].name) == 0) {
            *cmd = (sh_web_cmd_t)c;
            *code = cmds[c].code;
            return true;
        }
    }
    return false;
}

bool sh_web_parse_int(const char *s, int16_t *out)
{
    if (!s) {
        return false;
    }
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    }
    if (!*s) {
        return false;
    }
    long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        v = v * 10 + (*s - '0');
        if (v > 32768) {
            return false;
        }
    }
    if (neg) {
        v = -v;
    }
    if (v < INT16_MIN || v > INT16_MAX) {
        return false;
    }
    *out = (int16_t)v;
    return true;
}

bool sh_web_code_ok(const char *given, const char *ap_ssid)
{
    if (!given || !ap_ssid) {
        return false;
    }
    const size_t n = strlen(ap_ssid);
    if (n < SH_WEB_CODE_LEN || strlen(given) != SH_WEB_CODE_LEN) {
        return false;
    }
    const char *code = ap_ssid + n - SH_WEB_CODE_LEN;
    for (int i = 0; i < SH_WEB_CODE_LEN; i++) {
        if (tolower((unsigned char)given[i]) != tolower((unsigned char)code[i])) {
            return false;
        }
    }
    return true;
}

const char *sh_web_reason_key(sh_pump_reason_t reason)
{
    static const char *const keys[SH_PUMP_REASON_COUNT] = {
        [SH_PUMP_REASON_NONE]         = "none",
        [SH_PUMP_REASON_SENSOR]       = "sensor",
        [SH_PUMP_REASON_COL_BOILING]  = "col_boiling",
        [SH_PUMP_REASON_TANK_CEILING] = "tank_ceiling",
        [SH_PUMP_REASON_COL_PROTECT]  = "col_protect",
        [SH_PUMP_REASON_FROST]        = "frost",
        [SH_PUMP_REASON_FROST_TANK]   = "frost_tank",
        [SH_PUMP_REASON_TANK_MAX]     = "tank_max",
        [SH_PUMP_REASON_DIFF]         = "diff",
        [SH_PUMP_REASON_MIN_RUN]      = "min_run",
        [SH_PUMP_REASON_MIN_OFF]      = "min_off",
        [SH_PUMP_REASON_COL_MIN]      = "col_min",
        [SH_PUMP_REASON_TEST]         = "test",
        [SH_PUMP_REASON_ANTI_SEIZE]   = "anti_seize",
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
