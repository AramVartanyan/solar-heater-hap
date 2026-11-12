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

    sh_web_status_t s = {
        .t_col = st.t_col, .t_tank = st.t_tank, .col_fault = st.col_fault, .tank_fault = st.tank_fault,
        .pump_on = st.pump_on, .reason = st.pump_reason, .alarms = st.alarms,
        .test_on = st.test_on,
        .test_left_s = st.test_on && st.test_end_s > now_s ? st.test_end_s - now_s : 0,
        .col_protect = SH_CONTROL_COL_PROTECT,
        .name = SH_HOSTNAME, .version = esp_app_get_description()->version,
        .ssid = ssid, .ip = ip, .rssi = captive_wifi_rssi(),
    };
    get_config(&s.cfg);

    char *json = malloc(SH_WEB_STATUS_MAX);
    if (!json) {
        return httpd_resp_send_500(req);
    }
    const int len = sh_web_status_json(json, SH_WEB_STATUS_MAX, &s);
    esp_err_t err;
    if (len < 0) {
        err = httpd_resp_send_500(req);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        err = httpd_resp_send(req, json, len);
    }
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
    const sh_web_res_t res = sh_web_reset(body, s_code, &what);
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
        }
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
