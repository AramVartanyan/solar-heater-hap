/* Stand-in for the firmware, to try the page of the web interface in a browser on the computer
 * (doc/tasks/step6-installer-web.md 3.5): serves main/web/index.html and the API of main/sh_web.c
 * with the same plain-C code (sh_web_api, sh_installer, sh_settings, sh_pump_ctl, sh_alarm).
 * Nothing is stored; the values start at the Kconfig defaults.
 *
 *   make -C project/solar-heater-hap/test sim        ->  http://localhost:8080/
 *   build/web_sim ../main/web/index.html [port]
 *
 * The access code is 810841. The measurement is set by hand:
 *   http://localhost:8080/sim?col=1105&tank=720      temperatures, tenths of °C
 *   http://localhost:8080/sim?cf=1&tf=0              sensor faults (collector, tank)
 *   http://localhost:8080/sim?net=0                  answers stop for a while ("no connection")
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

static sh_web_config_t s_cfg = {
    .settings = { .v = { 70, 25, 6, 4, 300 } },
    .frost = true,
    .inst = { .v = { SH_FLUID_WATER, 0, 15, 120 } },
};
static sh_pump_temps_t s_temps = { .t_col = 523, .t_tank = 418 };
static sh_pump_ctl_t s_pump;
static sh_alarm_state_t s_alarm;
static const sh_alarm_params_t ALARM = {
    .e6_time_s = 600, .e6_col_rise = 100, .e6_tank_rise = 5, .e6_diff = 200, .e6_clear_diff = 100,
};
static time_t s_start, s_silent_until;

static uint32_t now_s(void)
{
    return (uint32_t)(time(NULL) - s_start);
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
    const sh_web_status_t s = {
        .t_col = s_temps.t_col, .t_tank = s_temps.t_tank,
        .col_fault = s_temps.col_fault, .tank_fault = s_temps.tank_fault,
        .pump_on = s_pump.on, .reason = s_pump.reason, .alarms = s_alarm.active,
        .test_on = s_pump.test, .test_left_s = sh_pump_ctl_test_left(&s_pump, now),
        .cfg = s_cfg, .col_protect = 1100,
        .name = "solar-heater", .version = "0.1.0 (sim)", .ssid = "A16blue", .ip = "192.168.1.57",
        .rssi = -41,
    };
    static char json[SH_WEB_STATUS_MAX];
    const int len = sh_web_status_json(json, sizeof(json), &s);
    reply(fd, len < 0 ? "500 Internal Server Error" : "200 OK", "application/json", json,
          len < 0 ? 0 : (size_t)len);
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
    } else if (post && strcmp(path, "/api/set") == 0) {
        unsigned changed;
        reply_res(fd, sh_web_set(body, CODE, &s_cfg, &changed));
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
        if (res == SH_WEB_OK && what != SH_WEB_RESET_WIFI) {
            s_cfg = (sh_web_config_t){ .settings = { .v = { 70, 25, 6, 4, 300 } }, .frost = true,
                                       .inst = { .v = { SH_FLUID_WATER, 0, 15, 120 } } };
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
    s_start = time(NULL);
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
