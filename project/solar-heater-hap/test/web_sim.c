/* Stand-in for the firmware, to try the page of the web interface in a browser on the computer
 * (doc/tasks/step6-installer-web.md 3.5): serves main/web/index.html and the API of main/sh_web.c
 * with the same plain-C code (sh_web_api, sh_installer, sh_settings, sh_pump_ctl, sh_alarm,
 * sh_clock, sh_history). Nothing is stored; the values start at the Kconfig defaults. The clock is
 * the one of the computer (set "by SNTP" at the start, the time zone not confirmed); the day so
 * far and ten daily summaries are made up at the start.
 *
 *   make -C project/solar-heater-hap/test sim        ->  http://localhost:8080/
 *   build/web_sim ../main/web/index.html [port]
 *
 * The access code is 810841. The measurement is set by hand:
 *   http://localhost:8080/sim?col=1105&tank=720      temperatures, tenths of °C
 *   http://localhost:8080/sim?cf=1&tf=0              sensor faults (collector, tank)
 *   http://localhost:8080/sim?net=0                  answers stop for a while ("no connection")
 *   http://localhost:8080/sim?clock=0                clock not set, as after a power loss: the next
 *                                                    opening of the page sets it; clock=1: by SNTP
 *   http://localhost:8080/sim?hist=0                 no "history" partition (hist=1: back)
 * POSIX sockets, one request at a time. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "sh_state.h"
#include "sh_pump_ctl.h"
#include "sh_alarm.h"
#include "sh_web_api.h"

#define CODE    "810841"
#define FLASH_SIZE  0x10000u    /* the "history" partition */

static sh_web_config_t s_cfg = {
    .settings = { .v = { 70, 25, 6, 4, 300 } },
    .frost = true,
    .inst = { .v = { SH_FLUID_WATER, 0, 15, 120 } },
    .tz = SH_ZONE_NONE,
};
static sh_pump_temps_t s_temps = { .t_col = 523, .t_tank = 418 };
static sh_pump_ctl_t s_pump;
static sh_alarm_state_t s_alarm;
static const sh_alarm_params_t ALARM = {
    .e6_time_s = 600, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200, .e6_clear_diff = 100,
};
static time_t s_start, s_silent_until;

/* the clock: the one of the computer, or what the browser sent */
static bool s_clock_set = true, s_sntp = true;
static time_t s_sntp_at, s_clock_offset;
/* the history: the day in RAM, the log in a "flash" in RAM */
static sh_hist_t s_hist;
static sh_hist_log_t s_log;
static bool s_log_ok = true;
static uint8_t s_mem[FLASH_SIZE];

static uint32_t now_s(void)
{
    return (uint32_t)(time(NULL) - s_start);
}

static bool mem_read(void *ctx, uint32_t off, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    memcpy(buf, s_mem + off, len);
    return true;
}

static bool mem_write(void *ctx, uint32_t off, const uint8_t *buf, uint32_t len)
{
    (void)ctx;
    for (uint32_t i = 0; i < len; i++) {
        s_mem[off + i] &= buf[i];
    }
    return true;
}

static bool mem_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    memset(s_mem + off, 0xFF, len);
    return true;
}

static const sh_hist_flash_t s_flash = {
    .size = FLASH_SIZE, .read = mem_read, .write = mem_write, .erase = mem_erase,
};

/* Local date and time at the monotonic second `now`, by the rule of the chosen zone. */
static void local_time(uint32_t now, uint16_t *day, uint32_t *sec)
{
    setenv("TZ", s_cfg.tz == SH_ZONE_NONE ? "UTC0" : sh_zones[s_cfg.tz].tz, 1);
    tzset();
    const time_t t = s_start + (time_t)now + s_clock_offset;
    struct tm tm;
    localtime_r(&t, &tm);
    *day = sh_clock_day(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    *sec = (uint32_t)(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
}

/* One step of the history, as sh_control does it after the pump and the codes. */
static void history_step(uint32_t now, const sh_pump_temps_t *t, bool pump_on, sh_pump_reason_t reason,
                         uint8_t alarms)
{
    sh_hist_in_t in = {
        .now_s = now, .t_col = t->t_col, .t_tank = t->t_tank, .col_fault = t->col_fault,
        .tank_fault = t->tank_fault, .pump_on = pump_on, .reason = reason, .alarms = alarms,
        .clock_set = s_clock_set, .zone_confirmed = s_cfg.tz != SH_ZONE_NONE,
    };
    local_time(now, &in.day, &in.sec);
    sh_hist_day_t day;
    if (sh_hist_step(&s_hist, &in, &day) && s_log_ok) {
        sh_hist_log_append(&s_log, &day);
    }
}

/* A made-up sunny day up to now (collector, tank, the pump charging, a short collector protection
 * and a pump test) and ten daily summaries before it. */
static void history_fill(void)
{
    memset(s_mem, 0xFF, sizeof(s_mem));
    sh_hist_log_open(&s_log, &s_flash);
    uint16_t today;
    uint32_t sec;
    local_time(0, &today, &sec);
    for (int i = 10; i >= 1; i--) {
        const sh_hist_day_t d = {
            .day = (uint16_t)(today - i),
            .col_max = (int16_t)(620 + 37 * (i % 4)), .col_min = (int16_t)(35 - 9 * i),
            .tank_max = (int16_t)(540 + 23 * (i % 3)), .tank_min = (int16_t)(380 + 11 * (i % 5)),
            .run_min = { (uint16_t)(150 + 17 * (i % 5)), (uint16_t)(i % 4 == 0 ? 6 : 0),
                         (uint16_t)(i % 3 == 0 ? 5 : 0) },
            .alarms = (uint16_t)(i == 4 ? SH_ALARM_BIT(SH_ALARM_E1) :
                                 i % 4 == 0 ? SH_ALARM_BIT(SH_ALARM_HOT) : 0),
            .peak_min = (uint16_t)(13 * 60 + 7 * i), .reliable = true,
            .sunrise = SH_HIST_NONE16, .sunset = SH_HIST_NONE16, .cool_target = SH_HIST_NONE8,
        };
        sh_hist_log_append(&s_log, &d);
    }
    /* the start of the stand-in moves back to the local midnight: the monotonic time runs from it */
    s_start -= (time_t)sec;
    sh_pump_temps_t t = { 0 };
    int tank = 400;
    for (uint32_t now = 0; now < sec; now++) {
        const int h100 = (int)(now / 36);               /* hours * 100 */
        int sun = h100 > 700 && h100 < 1900 ? 600 - (h100 - 1300) * (h100 - 1300) / 600 : 0;
        const bool protect = h100 >= 1330 && h100 < 1340, test = h100 >= 1700 && h100 < 1708;
        const bool charge = !protect && sun > 0 && 80 + sun > tank + 60 && tank < 700;
        if (charge && now % 90 == 0) {
            tank++;
        } else if (!charge && now % 400 == 0 && tank > 380) {
            tank--;
        }
        t.t_col = (int16_t)(protect ? 1105 : charge ? tank + 60 + sun / 12 : 80 + sun);
        t.t_tank = (int16_t)tank;
        history_step(now, &t, charge || protect || test,
                     protect ? SH_PUMP_REASON_COL_PROTECT : test ? SH_PUMP_REASON_TEST : SH_PUMP_REASON_DIFF,
                     protect ? SH_ALARM_BIT(SH_ALARM_HOT) : 0);
    }
}

/* One step of the control, as sh_control does it every second. */
static void control(void)
{
    sh_limits_t lim;
    sh_limits_calc(&s_cfg.inst, &lim);
    const sh_pump_params_t p = {
        .diff_on = (int16_t)(s_cfg.settings.v[SH_SET_DIFF_ON] * 10),
        .diff_off = (int16_t)(s_cfg.settings.v[SH_SET_DIFF_OFF] * 10),
        .min_run_s = 180, .min_off_s = 120,
        .tank_max = (int16_t)(s_cfg.settings.v[SH_SET_TANK_MAX] * 10),
        .col_min = (int16_t)(s_cfg.settings.v[SH_SET_COL_MIN] * 10),
        .col_protect = 1100, .col_protect_cap = lim.protect_cap, .col_boiling = lim.boiling,
        .frost_on = s_cfg.frost, .frost = lim.frost,
    };
    const uint32_t now = now_s();
    sh_pump_ctl_step(&s_pump, &p, &s_temps, now);
    s_pump.events = 0;
    const sh_alarm_in_t in = {
        .t_col = s_temps.t_col, .t_tank = s_temps.t_tank,
        .col_fault = s_temps.col_fault, .tank_fault = s_temps.tank_fault,
        .pump_on = s_pump.on, .reason = s_pump.reason,
        .col_boiling = s_pump.norm.col_boiling, .tank_ceiling = s_pump.norm.tank_ceiling,
    };
    sh_alarm_step(&s_alarm, &ALARM, &in, now);
    history_step(now, &s_temps, s_pump.on, s_pump.reason, s_alarm.active);
}

static void reply(int fd, const char *status, const char *type, const char *body, size_t len)
{
    char head[256];
    const int n = snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                           "Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, type, len);
    if (write(fd, head, (size_t)n) < 0 || write(fd, body, len) < 0) {
        perror("write");
    }
}

static void reply_res(int fd, sh_web_res_t res)
{
    static const char *const status[] = { "200 OK", "400 Bad Request", "403 Forbidden" };
    static const char *const body[] = { "{\"ok\":true}", "{\"ok\":false,\"err\":\"value\"}",
                                        "{\"ok\":false,\"err\":\"code\"}" };
    reply(fd, status[res], "application/json", body[res], strlen(body[res]));
}

static void reply_state(int fd)
{
    control();
    const uint32_t now = now_s();
    sh_web_status_t s = {
        .t_col = s_temps.t_col, .t_tank = s_temps.t_tank,
        .col_fault = s_temps.col_fault, .tank_fault = s_temps.tank_fault,
        .pump_on = s_pump.on, .reason = s_pump.reason, .alarms = s_alarm.active,
        .test_on = s_pump.test, .test_left_s = sh_pump_ctl_test_left(&s_pump, now),
        .clock_set = s_clock_set, .clock_ok = sh_hist_reliable(&s_hist),
        .peak_min = s_hist.peak.last_min, .peak_in = s_hist.peak.last_in, .history = s_log_ok,
        .cfg = s_cfg, .col_protect = 1100,
        .name = "solar-heater", .version = "0.1.0 (sim)", .ssid = "A16blue", .ip = "192.168.1.57",
        .rssi = -41,
    };
    uint16_t day;
    uint32_t sec;
    local_time(now, &day, &sec);
    s.clock_min = (uint16_t)(sec / 60);
    sh_hist_run_today(&s_hist, s.run_s);
    static char json[SH_WEB_STATUS_MAX];
    const int len = sh_web_status_json(json, sizeof(json), &s);
    reply(fd, len < 0 ? "500 Internal Server Error" : "200 OK", "application/json", json,
          len < 0 ? 0 : (size_t)len);
}

static void reply_history(int fd)
{
    control();
    static sh_hist_point_t pts[SH_HIST_SLOTS];
    static char json[SH_WEB_HISTORY_MAX];
    sh_hist_day_t days[SH_HIST_WEB_DAYS];
    bool day;
    const int n = sh_hist_chart(&s_hist, pts, &day);
    const int n_days = s_log_ok ? sh_hist_log_last(&s_log, days, SH_HIST_WEB_DAYS) : 0;
    const int len = sh_web_history_json(json, sizeof(json), pts, n, day, days, n_days);
    reply(fd, len < 0 ? "500 Internal Server Error" : "200 OK", "application/json", json,
          len < 0 ? 0 : (size_t)len);
}

static void reply_csv(int fd)
{
    static char csv[2048 * SH_HIST_CSV_LINE_MAX];
    size_t len = (size_t)snprintf(csv, sizeof(csv), "%s", sh_hist_csv_header());
    for (uint16_t i = 0; s_log_ok && i < s_log.count; i++) {
        sh_hist_day_t day;
        if (sh_hist_log_get(&s_log, i, &day)) {
            const int n = sh_hist_csv_line(csv + len, sizeof(csv) - len, &day);
            len += n > 0 ? (size_t)n : 0;
        }
    }
    reply(fd, "200 OK", "text/csv", csv, len);
}

/* /sim?key=value&... */
static void sim_set(const char *query)
{
    int v;
    const char *p;
    if ((p = strstr(query, "col=")) && sscanf(p + 4, "%d", &v) == 1) {
        s_temps.t_col = (int16_t)v;
    }
    if ((p = strstr(query, "tank=")) && sscanf(p + 5, "%d", &v) == 1) {
        s_temps.t_tank = (int16_t)v;
    }
    if ((p = strstr(query, "cf=")) && sscanf(p + 3, "%d", &v) == 1) {
        s_temps.col_fault = v != 0;
    }
    if ((p = strstr(query, "tf=")) && sscanf(p + 3, "%d", &v) == 1) {
        s_temps.tank_fault = v != 0;
    }
    if ((p = strstr(query, "net=")) && sscanf(p + 4, "%d", &v) == 1) {
        s_silent_until = v ? 0 : time(NULL) + 20;
    }
    if ((p = strstr(query, "clock=")) && sscanf(p + 6, "%d", &v) == 1) {
        s_clock_set = s_sntp = v != 0;
        s_sntp_at = time(NULL);
        s_clock_offset = 0;
    }
    if ((p = strstr(query, "hist=")) && sscanf(p + 5, "%d", &v) == 1) {
        s_log_ok = v != 0;
    }
}

static void handle(int fd, const char *page, size_t page_len)
{
    static char req[4096];
    size_t got = 0;
    char *body = NULL;
    size_t need = 0;
    /* the head, then as much body as Content-Length says */
    while (got < sizeof(req) - 1) {
        const ssize_t r = read(fd, req + got, sizeof(req) - 1 - got);
        if (r <= 0) {
            return;
        }
        got += (size_t)r;
        req[got] = '\0';
        if (!body && (body = strstr(req, "\r\n\r\n")) != NULL) {
            body += 4;
            const char *cl = strstr(req, "Content-Length:");
            if (!cl) {
                cl = strstr(req, "content-length:");
            }
            need = cl ? (size_t)atoi(cl + 15) : 0;
        }
        if (body && got - (size_t)(body - req) >= need) {
            break;
        }
    }
    if (!body) {
        return;
    }
    char method[8], path[128];
    if (sscanf(req, "%7s %127s", method, path) != 2) {
        return;
    }
    const bool post = strcmp(method, "POST") == 0;
    printf("%s %s %s\n", method, path, post ? body : "");

    if (strncmp(path, "/sim", 4) == 0) {
        sim_set(path);
        reply(fd, "200 OK", "text/plain", "ok\n", 3);
    } else if (time(NULL) < s_silent_until) {
        return;                                 /* the connection is just closed */
    } else if (!post && strcmp(path, "/") == 0) {
        reply(fd, "200 OK", "text/html; charset=utf-8", page, page_len);
    } else if (!post && strcmp(path, "/api/state") == 0) {
        reply_state(fd);
    } else if (!post && strcmp(path, "/api/history") == 0) {
        reply_history(fd);
    } else if (!post && strcmp(path, "/api/history.csv") == 0) {
        reply_csv(fd);
    } else if (!post && strcmp(path, "/api/zones") == 0) {
        static char json[SH_WEB_ZONES_MAX];
        const int len = sh_web_zones_json(json, sizeof(json));
        reply(fd, "200 OK", "application/json", json, len < 0 ? 0 : (size_t)len);
    } else if (post && strcmp(path, "/api/time") == 0) {
        uint32_t epoch = 0;
        const sh_web_res_t res = sh_web_time(body, &epoch);
        if (res == SH_WEB_OK && sh_clock_browser_ok(s_sntp, (uint32_t)(time(NULL) - s_sntp_at))) {
            s_clock_offset = (time_t)epoch - time(NULL);
            s_clock_set = true;
            printf("clock set by the browser\n");
        }
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/set") == 0) {
        unsigned changed = 0;
        const sh_web_res_t res = sh_web_set(body, CODE, &s_cfg, &changed);
        if (res == SH_WEB_OK && (changed & SH_WEB_CH_TZ)) {
            sh_hist_peak_reset(&s_hist, true);
        }
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/test") == 0) {
        sh_web_test_t cmd = SH_WEB_TEST_NONE;
        const sh_web_res_t res = sh_web_test(body, s_pump.test, &cmd);
        if (res == SH_WEB_OK && cmd == SH_WEB_TEST_PLUS) {
            sh_pump_ctl_plus(&s_pump, (uint32_t)s_cfg.settings.v[SH_SET_TEST_S], now_s());
        } else if (res == SH_WEB_OK && cmd == SH_WEB_TEST_MINUS) {
            sh_pump_ctl_minus(&s_pump, now_s());
        } else if (res == SH_WEB_OK && cmd == SH_WEB_TEST_STOP) {
            sh_pump_ctl_stop(&s_pump, now_s());
        }
        control();
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/reset") == 0) {
        sh_web_reset_t what;
        const sh_web_res_t res = sh_web_reset(body, CODE, &what);
        if (res == SH_WEB_OK && what == SH_WEB_RESET_LEARNED) {
            sh_hist_peak_reset(&s_hist, s_cfg.tz != SH_ZONE_NONE);
        } else if (res == SH_WEB_OK && what == SH_WEB_RESET_HISTORY) {
            sh_hist_log_clear(&s_log);
        } else if (res == SH_WEB_OK && what != SH_WEB_RESET_WIFI) {
            s_cfg = (sh_web_config_t){ .settings = { .v = { 70, 25, 6, 4, 300 } }, .frost = true,
                                       .inst = { .v = { SH_FLUID_WATER, 0, 15, 120 } },
                                       .tz = what == SH_WEB_RESET_FACTORY ? SH_ZONE_NONE : s_cfg.tz };
        }
        reply_res(fd, res);
    } else if (post && strcmp(path, "/api/auth") == 0) {
        reply_res(fd, sh_web_code_ok(body, CODE) ? SH_WEB_OK : SH_WEB_DENIED);
    } else {
        reply(fd, "404 Not Found", "text/plain", "not found\n", 10);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <index.html> [port]\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    static char page[256 * 1024];
    const size_t page_len = fread(page, 1, sizeof(page), f);
    fclose(f);
    const int port = argc > 2 ? atoi(argv[2]) : 8080;

    const int srv = socket(AF_INET, SOCK_STREAM, 0);
    const int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (srv < 0 || bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(srv, 8) < 0) {
        perror("listen");
        return 1;
    }
    s_start = s_sntp_at = time(NULL);
    history_fill();
    printf("page %zu bytes, http://localhost:%d/ (access code %s)\n", page_len, port, CODE);
    fflush(stdout);
    for (;;) {
        const int fd = accept(srv, NULL, NULL);
        if (fd < 0) {
            continue;
        }
        handle(fd, page, page_len);
        fflush(stdout);
        close(fd);
    }
}
