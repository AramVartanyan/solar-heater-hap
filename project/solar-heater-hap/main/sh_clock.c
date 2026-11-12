#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif_sntp.h"
#include "nvs.h"
#include "sh_clock.h"

static const char *TAG = "sh_clock";

#define SNTP_SERVER     "pool.ntp.org"
#define NVS_NAMESPACE   "sh_clock"
#define KEY_TZ          "tz"            /* string: IANA name */
#define KEY_RULE        "tz_rule"       /* string: POSIX TZ */
#define KEY_TZ_OK       "tz_ok"         /* u8: confirmed by the installer */
#define KEY_PEAK        "peak"          /* blob: reliable, in_row, out_row, peak (i16 LE) */

static SemaphoreHandle_t s_lock;
static char s_tz[SH_TZ_NAME_MAX];
static char s_rule[SH_TZ_RULE_MAX];
static bool s_tz_ok;
static sh_peak_t s_peak;
static bool s_set, s_sntp, s_sntp_ever;
static uint32_t s_sntp_s;

static uint32_t now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void apply_rule(void)
{
    setenv("TZ", s_rule, 1);
    tzset();
}

/* The TZ rule and the local time: always with the lock held (setenv() and localtime_r() of different
 * tasks must not meet). */
static void log_time(const char *what)
{
    const time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    ESP_LOGI(TAG, "%s: %s (%s)", what, buf, s_tz);
}

/* Called with the lock held. */
static esp_err_t save(bool zone)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        if (zone) {
            err = nvs_set_str(h, KEY_TZ, s_tz);
            if (err == ESP_OK) {
                err = nvs_set_str(h, KEY_RULE, s_rule);
            }
            if (err == ESP_OK) {
                err = nvs_set_u8(h, KEY_TZ_OK, s_tz_ok);
            }
        }
        const uint8_t blob[5] = { s_peak.reliable, s_peak.in_row, s_peak.out_row,
                                  (uint8_t)s_peak.peak, (uint8_t)((uint16_t)s_peak.peak >> 8) };
        if (err == ESP_OK) {
            err = nvs_set_blob(h, KEY_PEAK, blob, sizeof(blob));
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "not saved: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t sh_clock_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(s_tz, sh_tz_list[SH_TZ_DEFAULT].name, sizeof(s_tz));
    strlcpy(s_rule, sh_tz_list[SH_TZ_DEFAULT].rule, sizeof(s_rule));
    s_tz_ok = false;
    bool peak_stored = false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        char tz[SH_TZ_NAME_MAX], rule[SH_TZ_RULE_MAX];
        size_t tz_len = sizeof(tz), rule_len = sizeof(rule);
        uint8_t ok = 0;
        if (nvs_get_str(h, KEY_TZ, tz, &tz_len) == ESP_OK && nvs_get_str(h, KEY_RULE, rule, &rule_len) == ESP_OK) {
            strlcpy(s_tz, tz, sizeof(s_tz));
            strlcpy(s_rule, rule, sizeof(s_rule));
            s_tz_ok = nvs_get_u8(h, KEY_TZ_OK, &ok) == ESP_OK && ok == 1;
        }
        uint8_t blob[5];
        size_t len = sizeof(blob);
        if (nvs_get_blob(h, KEY_PEAK, blob, &len) == ESP_OK && len == sizeof(blob) && blob[0] <= 1) {
            s_peak.reliable = blob[0];
            s_peak.in_row = blob[1];
            s_peak.out_row = blob[2];
            s_peak.peak = (int16_t)(blob[3] | blob[4] << 8);
            peak_stored = true;
        }
        nvs_close(h);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "NVS open: %s, defaults used", esp_err_to_name(err));
    }
    if (!peak_stored) {
        sh_peak_reset(&s_peak, s_tz_ok);
    }
    apply_rule();
    ESP_LOGI(TAG, "time zone %s (%s), %s; peak check: %s", s_tz, s_rule, s_tz_ok ? "confirmed" : "not confirmed",
             s_peak.reliable ? "reliable" : "not reliable");
    return ESP_OK;
}

static void sntp_synced(struct timeval *tv)
{
    (void)tv;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool first = !s_sntp_ever;
    s_set = true;
    s_sntp = true;
    s_sntp_ever = true;
    s_sntp_s = now_s();
    log_time(first ? "clock set by SNTP" : "clock synchronised by SNTP");
    xSemaphoreGive(s_lock);
}

esp_err_t sh_clock_start(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
    cfg.start = false;                  /* at the connection to the router */
    cfg.sync_cb = sntp_synced;
    const esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP: %s", esp_err_to_name(err));
    }
    return err;
}

void sh_clock_connected(void)
{
    const esp_err_t err = esp_netif_sntp_start();       /* starts, or restarts with a new request */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP start: %s", esp_err_to_name(err));
    }
}

void sh_clock_get(sh_clock_info_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->set = s_set;
    out->sntp = s_sntp;
    out->tz_confirmed = s_tz_ok;
    out->peak = s_peak;
    strlcpy(out->tz, s_tz, sizeof(out->tz));
    out->epoch = time(NULL);
    out->lt.set = false;
    out->lt.day = SH_DAY_NONE;
    out->lt.min = 0;
    if (out->set) {
        struct tm tm;
        localtime_r(&out->epoch, &tm);
        out->lt.day = sh_day_number(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        out->lt.min = (uint16_t)(tm.tm_hour * 60 + tm.tm_min);
        out->lt.set = out->lt.day != SH_DAY_NONE;
    }
    xSemaphoreGive(s_lock);
    out->trust = sh_clock_trust(out->lt.set, out->tz_confirmed, out->peak.reliable);
}

bool sh_clock_browser(int64_t epoch_s)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = sh_clock_browser_ok(epoch_s, s_sntp_ever, s_sntp_s, now_s());
    bool first = false;
    if (ok) {
        const time_t was = time(NULL);
        const struct timeval tv = { .tv_sec = (time_t)epoch_s };
        settimeofday(&tv, NULL);
        first = !s_set || labs((long)(epoch_s - was)) > 60;     /* the log: only a real change */
        s_set = true;
        s_sntp = false;
    }
    if (first) {
        log_time("clock set by the browser");
    }
    xSemaphoreGive(s_lock);
    return ok;
}

esp_err_t sh_clock_set_tz(int tz)
{
    if (tz < 0 || (size_t)tz >= sh_tz_count) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_tz, sh_tz_list[tz].name, sizeof(s_tz));
    strlcpy(s_rule, sh_tz_list[tz].rule, sizeof(s_rule));
    s_tz_ok = true;
    sh_peak_reset(&s_peak, true);       /* reliable at once, the check goes on */
    apply_rule();
    const esp_err_t err = save(true);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "time zone %s (%s) confirmed", sh_tz_list[tz].name, sh_tz_list[tz].rule);
    return err;
}

void sh_clock_day(const sh_day_sum_t *day)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_tz_ok) {
        xSemaphoreGive(s_lock);
        return;                         /* not reliable anyway until the zone is confirmed */
    }
    const bool was = s_peak.reliable;
    const sh_peak_t before = s_peak;
    const sh_peak_day_t res = sh_peak_day(&s_peak, day->full, !day->col_fault, day->col_min, day->col_max,
                                          day->peak_min);
    if (before.reliable != s_peak.reliable || before.in_row != s_peak.in_row ||
        before.out_row != s_peak.out_row || before.peak != s_peak.peak) {
        save(false);
    }
    const bool now = s_peak.reliable;
    xSemaphoreGive(s_lock);

    if (res != SH_PEAK_NOT_COUNTED) {
        ESP_LOGI(TAG, "collector peak %02d:%02d %s the window", day->peak_min / 60, day->peak_min % 60,
                 res == SH_PEAK_IN ? "in" : "out of");
    }
    if (was != now) {
        ESP_LOGW(TAG, "clock %s by the collector peaks", now ? "reliable again" : "NOT reliable");
    }
}

esp_err_t sh_clock_reset_learned(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    sh_peak_reset(&s_peak, s_tz_ok);
    const esp_err_t err = save(false);
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "learned values reset (time zone check)");
    return err;
}
