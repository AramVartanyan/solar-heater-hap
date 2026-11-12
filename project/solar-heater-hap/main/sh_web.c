#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "captive_wifi.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_settings_store.h"
#include "sh_installer_store.h"
#include "sh_control.h"
#include "sh_reset.h"
#include "sh_clock.h"
#include "sh_history.h"
#include "sh_web_api.h"
#include "sh_web.h"

static const char *TAG = "sh_web";

#define BODY_MAX        128     /* longest request body with its NUL */

/* main/web/index.html (EMBED_FILES) */
extern const char page_start[] asm("_binary_index_html_start");
extern const char page_end[] asm("_binary_index_html_end");

static httpd_handle_t s_httpd;      /* only the web task starts and stops it */
static TaskHandle_t s_task;
static volatile bool s_enabled;
static char s_code[SH_WEB_CODE_LEN + 1];

static void get_config(sh_web_config_t *cfg)
{
    sh_settings_get(&cfg->settings);
    cfg->frost = sh_settings_frost();
    sh_installer_get(&cfg->inst, NULL);
}

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, page_start, page_end - page_start);
}

static esp_err_t send_json(httpd_req_t *req, const char *json, int len)
{
    if (len < 0) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, len);
}

static esp_err_t state_get(httpd_req_t *req)
{
    sh_state_t st;
    sh_state_get(&st);
    const uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);

    wifi_ap_record_t ap;
    char ssid[sizeof(ap.ssid) + 1] = "";
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        memcpy(ssid, ap.ssid, sizeof(ap.ssid));     /* 32 characters have no NUL */
    }
    char ip[16] = "";
    captive_wifi_get_ip(ip, sizeof(ip));

    sh_clock_info_t ci;
    sh_clock_get(&ci);

    sh_web_status_t s = {
        .t_col = st.t_col, .t_tank = st.t_tank, .col_fault = st.col_fault, .tank_fault = st.tank_fault,
        .pump_on = st.pump_on, .reason = st.pump_reason, .alarms = st.alarms,
        .test_on = st.test_on,
        .test_left_s = st.test_on && st.test_end_s > now_s ? st.test_end_s - now_s : 0,
        .col_protect = SH_CONTROL_COL_PROTECT,
        .name = SH_HOSTNAME, .version = esp_app_get_description()->version,
        .ssid = ssid, .ip = ip, .rssi = captive_wifi_rssi(),
        .clock = {
            .trust = ci.trust, .sntp = ci.sntp, .min = ci.lt.set ? (int16_t)ci.lt.min : -1,
            .tz = ci.tz, .tz_confirmed = ci.tz_confirmed, .peak = ci.peak.peak,
        },
        .history = sh_history_on(),
    };
    get_config(&s.cfg);
    sh_day_run_today(sh_history_day_lock(), &ci.lt, now_s, s.run_s);
    sh_history_day_unlock();

    char *json = malloc(SH_WEB_STATUS_MAX);
    if (!json) {
        return httpd_resp_send_500(req);
    }
    const esp_err_t err = send_json(req, json, sh_web_status_json(json, SH_WEB_STATUS_MAX, &s));
    free(json);
    return err;
}

static esp_err_t chart_get(httpd_req_t *req)
{
    char *json = malloc(SH_WEB_CHART_MAX);
    if (!json) {
        return httpd_resp_send_500(req);
    }
    sh_clock_info_t ci;
    sh_clock_get(&ci);
    const uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);
    const int len = sh_web_chart_json(json, SH_WEB_CHART_MAX, sh_history_day_lock(), &ci.lt, now_s);
    sh_history_day_unlock();
    const esp_err_t err = send_json(req, json, len);
    free(json);
    return err;
}

typedef struct {
    sh_hist_rec_t rec[SH_WEB_DAYS];
    unsigned n;
} days_t;

static bool day_take(void *ctx, const sh_hist_rec_t *r)
{
    days_t *d = ctx;
    d->rec[d->n++] = *r;
    return true;
}

static esp_err_t days_get(httpd_req_t *req)
{
    days_t *d = calloc(1, sizeof(days_t));
    char *json = malloc(SH_WEB_DAYS_MAX);
    esp_err_t err;
    if (!d || !json) {
        err = httpd_resp_send_500(req);
    } else {
        sh_history_walk(true, SH_WEB_DAYS, day_take, d);
        err = send_json(req, json, sh_web_days_json(json, SH_WEB_DAYS_MAX, sh_history_on(), d->rec, d->n));
    }
    free(d);
    free(json);
    return err;
}

#define CSV_CHUNK       1024

typedef struct {
    httpd_req_t *req;
    char buf[CSV_CHUNK + SH_HIST_CSV_LINE_MAX];
    size_t len;
    bool fail;
} csv_t;

static bool csv_flush(csv_t *c)
{
    if (c->len && !c->fail) {
        c->fail = httpd_resp_send_chunk(c->req, c->buf, (ssize_t)c->len) != ESP_OK;
    }
    c->len = 0;
    return !c->fail;
}

static bool csv_line(void *ctx, const sh_hist_rec_t *r)
{
    csv_t *c = ctx;
    const int n = sh_hist_csv_line(c->buf + c->len, sizeof(c->buf) - c->len, r);
    if (n > 0) {
        c->len += (size_t)n;
    }
    return c->len < CSV_CHUNK || csv_flush(c);
}

/* All the daily summaries, oldest first, sent in chunks. */
static esp_err_t csv_get(httpd_req_t *req)
{
    if (!sh_history_on()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no history");
    }
    csv_t *c = malloc(sizeof(csv_t));
    if (!c) {
        return httpd_resp_send_500(req);
    }
    c->req = req;
    c->fail = false;
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"" SH_HOSTNAME "-history.csv\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    c->len = strlcpy(c->buf, sh_hist_csv_header, sizeof(c->buf));
    sh_history_walk(false, 0, csv_line, c);
    csv_flush(c);
    const bool fail = c->fail;
    free(c);
    if (fail) {
        return ESP_FAIL;                /* the connection is gone */
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t tz_get(httpd_req_t *req)
{
    char *json = malloc(SH_WEB_TZ_MAX);
    if (!json) {
        return httpd_resp_send_500(req);
    }
    const esp_err_t err = send_json(req, json, sh_web_tz_json(json, SH_WEB_TZ_MAX));
    free(json);
    return err;
}

/* The whole body as a string; false (and the answer sent) if it is missing or too long. */
static bool read_body(httpd_req_t *req, char *body)
{
    const size_t len = req->content_len;
    if (len == 0 || len >= BODY_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");
        return false;
    }
    size_t got = 0;
    while (got < len) {
        const int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return false;               /* otherwise the connection is closed: no answer */
        }
        got += (size_t)r;
    }
    body[len] = '\0';
    return true;
}

static esp_err_t answer(httpd_req_t *req, sh_web_res_t res)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    switch (res) {
    case SH_WEB_OK:
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    case SH_WEB_DENIED:
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"code\"}");
    case SH_WEB_BAD:
    default:
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"value\"}");
    }
}

static esp_err_t set_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    if (!read_body(req, body)) {
        return ESP_FAIL;
    }
    sh_web_config_t cfg;
    get_config(&cfg);
    unsigned changed = 0;
    sh_web_res_t res = sh_web_set(body, s_code, &cfg, &changed);
    if (res == SH_WEB_OK) {
        /* each store writes only what differs; a value in use is kept also if the write fails */
        if (((changed & SH_WEB_CH_SETTINGS) && sh_settings_put(&cfg.settings) == ESP_ERR_INVALID_ARG) ||
            ((changed & SH_WEB_CH_INSTALLER) && sh_installer_set(&cfg.inst) == ESP_ERR_INVALID_ARG)) {
            res = SH_WEB_BAD;           /* changed by the menu meanwhile */
        }
        if (changed & SH_WEB_CH_FROST) {
            sh_settings_set_frost(cfg.frost);
        }
    }
    return answer(req, res);
}

static esp_err_t test_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    if (!read_body(req, body)) {
        return ESP_FAIL;
    }
    sh_state_t st;
    sh_state_get(&st);
    sh_web_test_t cmd = SH_WEB_TEST_NONE;
    const sh_web_res_t res = sh_web_test(body, st.test_on, &cmd);
    if (res == SH_WEB_OK) {
        switch (cmd) {
        case SH_WEB_TEST_PLUS:
            sh_control_cmd(SH_CONTROL_TEST_PLUS);
            break;
        case SH_WEB_TEST_MINUS:
            sh_control_cmd(SH_CONTROL_TEST_MINUS);
            break;
        case SH_WEB_TEST_STOP:
            sh_control_cmd(SH_CONTROL_TEST_STOP);
            break;
        case SH_WEB_TEST_NONE:
            break;
        }
    }
    return answer(req, res);
}

static esp_err_t reset_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    if (!read_body(req, body)) {
        return ESP_FAIL;
    }
    sh_web_reset_t what = SH_WEB_RESET_WIFI;
    sh_web_res_t res = sh_web_reset(body, s_code, &what);
    if (res == SH_WEB_OK) {
        switch (what) {
        case SH_WEB_RESET_DEFAULTS:     /* applies at once, no restart */
            ESP_LOGW(TAG, "web: settings reset to the defaults");
            sh_settings_reset();
            sh_installer_reset();
            break;
        case SH_WEB_RESET_WIFI:         /* after the answer: the restart closes the connection */
            ESP_LOGW(TAG, "web: Wi-Fi reset");
            sh_reset_later(SH_RESET_WIFI);
            break;
        case SH_WEB_RESET_FACTORY:
            ESP_LOGW(TAG, "web: factory reset");
            sh_reset_later(SH_RESET_FACTORY);
            break;
        case SH_WEB_RESET_HISTORY:
            ESP_LOGW(TAG, "web: history reset");
            if (sh_history_clear() != ESP_OK) {
                res = SH_WEB_BAD;       /* no partition, or a flash error */
            }
            break;
        case SH_WEB_RESET_LEARNED:
            ESP_LOGW(TAG, "web: learned values reset");
            sh_clock_reset_learned();
            break;
        }
    }
    return answer(req, res);
}

/* The time of the browser: no code; the clock decides whether it takes it. */
static esp_err_t time_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    if (!read_body(req, body)) {
        return ESP_FAIL;
    }
    int64_t t;
    const sh_web_res_t res = sh_web_time(body, &t);
    if (res != SH_WEB_OK) {
        return answer(req, res);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, sh_clock_browser(t) ? "{\"ok\":true,\"taken\":1}" : "{\"ok\":true,\"taken\":0}");
}

static esp_err_t tz_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    if (!read_body(req, body)) {
        return ESP_FAIL;
    }
    int tz;
    sh_web_res_t res = sh_web_tz(body, s_code, &tz);
    if (res == SH_WEB_OK && sh_clock_set_tz(tz) == ESP_ERR_INVALID_ARG) {
        res = SH_WEB_BAD;
    }
    return answer(req, res);
}

static esp_err_t auth_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    if (!read_body(req, body)) {
        return ESP_FAIL;
    }
    return answer(req, sh_web_code_ok(body, s_code) ? SH_WEB_OK : SH_WEB_DENIED);
}

static void server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 12;
    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        s_httpd = NULL;
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/",          .method = HTTP_GET,  .handler = page_get },
        { .uri = "/api/state", .method = HTTP_GET,  .handler = state_get },
        { .uri = "/api/set",   .method = HTTP_POST, .handler = set_post },
        { .uri = "/api/test",  .method = HTTP_POST, .handler = test_post },
        { .uri = "/api/reset", .method = HTTP_POST, .handler = reset_post },
        { .uri = "/api/auth",  .method = HTTP_POST, .handler = auth_post },
        { .uri = "/api/chart", .method = HTTP_GET,  .handler = chart_get },
        { .uri = "/api/days",  .method = HTTP_GET,  .handler = days_get },
        { .uri = "/api/history.csv", .method = HTTP_GET, .handler = csv_get },
        { .uri = "/api/tz",    .method = HTTP_GET,  .handler = tz_get },
        { .uri = "/api/time",  .method = HTTP_POST, .handler = time_post },
        { .uri = "/api/tz",    .method = HTTP_POST, .handler = tz_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_httpd, &uris[i]);
    }
    char ip[16] = "";
    captive_wifi_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "web interface: http://%s/ (access code %s)", ip, s_code);
}

static void server_stop(void)
{
    httpd_stop(s_httpd);
    s_httpd = NULL;
    ESP_LOGI(TAG, "web interface stopped");
}

/* Starts and stops the server outside the event loop task (its stack is too small for that). */
static void web_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const bool on = s_enabled;
        if (on && !s_httpd) {
            server_start();
        } else if (!on && s_httpd) {
            server_stop();
        }
    }
}

void sh_web_enable(bool on)
{
    s_enabled = on;
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

esp_err_t sh_web_init(void)
{
    /* the access code: the last 6 characters of "<prefix>-XXXXXX" */
    const char *ssid = captive_wifi_ap_ssid();
    const size_t len = strlen(ssid);
    strlcpy(s_code, len >= SH_WEB_CODE_LEN ? ssid + len - SH_WEB_CODE_LEN : ssid, sizeof(s_code));

    /* above the captive-wifi worker (3): the server is down before the portal can start its own */
    if (xTaskCreate(web_task, "sh_web", 4096, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "web task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
