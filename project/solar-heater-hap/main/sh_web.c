#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
#include "sh_web_data.h"
#include "sh_web.h"

static const char *TAG = "sh_web";

#define BODY_MAX        96          /* the longest request body: "k=col_max&v=250" and the like */
#define JSON_MAX        1536

/* web/index.html (main/CMakeLists.txt: EMBED_TXTFILES, NUL-terminated) */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_server;
static SemaphoreHandle_t s_lock;
static char s_json[JSON_MAX];       /* only the server task uses it */

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static void snapshot(sh_web_snap_t *s, char *ssid, size_t ssid_size, char *ip, size_t ip_size)
{
    sh_state_t st;
    sh_state_get(&st);
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);

    memset(s, 0, sizeof(*s));
    s->t_col = st.t_col;
    s->t_tank = st.t_tank;
    s->col_fault = st.col_fault;
    s->tank_fault = st.tank_fault;
    s->pump_on = st.pump_on;
    s->reason = st.pump_reason;
    s->test_on = st.test_on;
    s->test_left_s = st.test_on && st.test_end_s > now ? st.test_end_s - now : 0;
    s->alarms = st.alarms;
    sh_settings_get(&s->settings);
    s->frost_on = sh_settings_frost_get();
    sh_installer_get(&s->inst, &s->limits);
    s->col_protect = sh_installer_col_protect();
    s->col_protect_used = sh_installer_col_protect_used(&s->limits);
    s->version = esp_app_get_description()->version;
    s->host = SH_HOSTNAME;

    ssid[0] = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        strlcpy(ssid, (const char *)ap.ssid, ssid_size);
        s->rssi = ap.rssi;
    }
    if (captive_wifi_get_ip(ip, ip_size) != ESP_OK) {
        ip[0] = 0;
    }
    s->ssid = ssid;
    s->ip = ip;
}

static esp_err_t state_get(httpd_req_t *req)
{
    sh_web_snap_t s;
    char ssid[33], ip[16];
    snapshot(&s, ssid, sizeof(ssid), ip, sizeof(ip));
    const size_t n = sh_web_state_json(&s, s_json, sizeof(s_json));
    if (n == 0) {
        ESP_LOGE(TAG, "state JSON does not fit %d B", JSON_MAX);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_json, n);
}

/* {"ok":true} or an error status with {"ok":false,"err":"<why>"} */
static esp_err_t reply(httpd_req_t *req, const char *status, const char *err)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!err) {
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":\"%s\"}", err);
    httpd_resp_set_status(req, status);
    return httpd_resp_sendstr(req, buf);
}

/* The form body of a POST (application/x-www-form-urlencoded), NUL-terminated. */
static bool read_body(httpd_req_t *req, char *buf, size_t size)
{
    if (req->content_len == 0 || req->content_len >= size) {
        return false;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        got += (size_t)n;
    }
    buf[got] = 0;
    return true;
}

/* The code the page keeps in its cookie "code" is checked on every change that needs it. */
static bool code_ok(httpd_req_t *req)
{
    char code[16];
    size_t len = sizeof(code);
    return httpd_req_get_cookie_val(req, "code", code, &len) == ESP_OK &&
           sh_web_code_ok(code, captive_wifi_ap_ssid());
}

static esp_err_t code_post(httpd_req_t *req)
{
    char body[BODY_MAX], code[16];
    if (!read_body(req, body, sizeof(body)) ||
        httpd_query_key_value(body, "code", code, sizeof(code)) != ESP_OK) {
        return reply(req, "400 Bad Request", "request");
    }
    if (!sh_web_code_ok(code, captive_wifi_ap_ssid())) {
        ESP_LOGW(TAG, "wrong access code");
        return reply(req, "403 Forbidden", "code");
    }
    return reply(req, NULL, NULL);
}

static esp_err_t set_post(httpd_req_t *req)
{
    char body[BODY_MAX], key[16], val[8];
    sh_web_param_t p;
    int16_t v;
    if (!read_body(req, body, sizeof(body)) ||
        httpd_query_key_value(body, "k", key, sizeof(key)) != ESP_OK ||
        httpd_query_key_value(body, "v", val, sizeof(val)) != ESP_OK ||
        !sh_web_param_find(key, &p) || !sh_web_parse_int(val, &v)) {
        return reply(req, "400 Bad Request", "request");
    }
    if (p.code && !code_ok(req)) {
        return reply(req, "403 Forbidden", "code");
    }
    esp_err_t err;
    switch (p.group) {
    case SH_WEB_PARAM_SETTING:
        err = sh_settings_set((sh_setting_t)p.id, v);          /* the checks of the menu */
        break;
    case SH_WEB_PARAM_FROST:
        err = v == 0 || v == 1 ? sh_settings_frost_set(v == 1) : ESP_ERR_INVALID_ARG;
        break;
    case SH_WEB_PARAM_INSTALLER:
    default:
        err = sh_installer_set((sh_inst_id_t)p.id, v);
        break;
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return reply(req, "400 Bad Request", "value");
    }
    if (err != ESP_OK) {
        return reply(req, "500 Internal Server Error", "save");
    }
    return reply(req, NULL, NULL);
}

static esp_err_t cmd_post(httpd_req_t *req)
{
    char body[BODY_MAX], name[24];
    sh_web_cmd_t cmd;
    bool needs_code;
    if (!read_body(req, body, sizeof(body)) ||
        httpd_query_key_value(body, "c", name, sizeof(name)) != ESP_OK ||
        !sh_web_cmd_find(name, &cmd, &needs_code)) {
        return reply(req, "400 Bad Request", "request");
    }
    if (needs_code && !code_ok(req)) {
        return reply(req, "403 Forbidden", "code");
    }
    switch (cmd) {
    case SH_WEB_CMD_TEST_PLUS:
        sh_control_cmd(SH_CONTROL_TEST_PLUS);
        break;
    case SH_WEB_CMD_TEST_MINUS:
        sh_control_cmd(SH_CONTROL_TEST_MINUS);
        break;
    case SH_WEB_CMD_TEST_STOP:
        sh_control_cmd(SH_CONTROL_TEST_STOP);
        break;
    case SH_WEB_CMD_RESET_SETTINGS:
        ESP_LOGW(TAG, "web: settings reset");
        sh_reset_now(SH_RESET_SETTINGS);
        break;
    case SH_WEB_CMD_RESET_WIFI:
        ESP_LOGW(TAG, "web: Wi-Fi reset");
        sh_reset_later(SH_RESET_WIFI);          /* after the answer */
        break;
    case SH_WEB_CMD_RESET_FACTORY:
    default:
        ESP_LOGW(TAG, "web: factory reset");
        sh_reset_later(SH_RESET_FACTORY);
        break;
    }
    return reply(req, NULL, NULL);
}

esp_err_t sh_web_start(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (!s_server) {
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.server_port = 80;
        cfg.stack_size = 6144;          /* the JSON (vsnprintf) and the NVS writes run in the handlers */
        cfg.lru_purge_enable = true;
        err = httpd_start(&s_server, &cfg);
        if (err == ESP_OK) {
            static const httpd_uri_t uris[] = {
                { .uri = "/",          .method = HTTP_GET,  .handler = page_get },
                { .uri = "/api/state", .method = HTTP_GET,  .handler = state_get },
                { .uri = "/api/set",   .method = HTTP_POST, .handler = set_post },
                { .uri = "/api/cmd",   .method = HTTP_POST, .handler = cmd_post },
                { .uri = "/api/code",  .method = HTTP_POST, .handler = code_post },
            };
            for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
                httpd_register_uri_handler(s_server, &uris[i]);
            }
            ESP_LOGI(TAG, "web interface on port 80 (page %u B)",
                     (unsigned)(index_html_end - index_html_start - 1));
        } else {
            s_server = NULL;
            ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

void sh_web_stop(void)
{
    if (!s_lock) {
        return;                     /* never started */
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "web interface stopped");
    }
    xSemaphoreGive(s_lock);
}
