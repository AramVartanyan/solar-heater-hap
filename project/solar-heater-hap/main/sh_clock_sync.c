#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "sh_clock_sync.h"

static const char *TAG = "sh_clock";

#define NVS_NAMESPACE   "sh_clock"
#define NVS_KEY_TZ      "tz"
#define SNTP_SERVER     "pool.ntp.org"
#define ZONE_NAME_MAX   40

static SemaphoreHandle_t s_lock;    /* the fields below, and the TZ of the C library */
static bool s_set;
static bool s_sntp;                 /* set by SNTP since boot ... */
static uint32_t s_sntp_s;           /* ... at this monotonic time */
static int16_t s_zone = SH_ZONE_NONE;

static uint32_t now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* Called with the lock held (or before the tasks start). */
static void use_zone(int zone)
{
    s_zone = (int16_t)zone;
    setenv("TZ", zone == SH_ZONE_NONE ? "UTC0" : sh_zones[zone].tz, 1);
    tzset();
}

/* lwIP calls it after it has set the system time. */
static void sntp_synced(struct timeval *tv)
{
    (void)tv;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_set = true;
    s_sntp = true;
    s_sntp_s = now_s();
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "clock set by SNTP");
}

esp_err_t sh_clock_sync_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    int zone = SH_ZONE_NONE;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        char name[ZONE_NAME_MAX];
        size_t len = sizeof(name);
        if (nvs_get_str(h, NVS_KEY_TZ, name, &len) == ESP_OK) {
            zone = sh_zone_find(name);
            if (zone == SH_ZONE_NONE) {
                ESP_LOGW(TAG, "stored time zone %s is not supported, UTC used", name);
            }
        }
        nvs_close(h);
    }
    use_zone(zone);
    ESP_LOGI(TAG, "time zone %s", zone == SH_ZONE_NONE ? "not confirmed (UTC)" : sh_zones[zone].name);
    return ESP_OK;
}

void sh_clock_sync_connected(void)
{
    if (esp_sntp_enabled()) {
        sntp_restart();                 /* asks now, then every 12 h again */
        return;
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, SNTP_SERVER);
    sntp_set_time_sync_notification_cb(sntp_synced);
    sntp_set_sync_interval(SH_CLOCK_SNTP_PERIOD_S * 1000u);
    esp_sntp_init();
}

bool sh_clock_sync_browser(uint32_t epoch_s)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool take = sh_clock_browser_ok(s_sntp, now_s() - s_sntp_s);
    if (take) {
        const struct timeval tv = { .tv_sec = (time_t)epoch_s };
        settimeofday(&tv, NULL);
        s_set = true;
    }
    xSemaphoreGive(s_lock);
    if (take) {
        ESP_LOGI(TAG, "clock set by the browser");
    }
    return take;
}

void sh_clock_now(sh_clock_now_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    out->set = s_set;
    out->zone = s_zone;
    xSemaphoreGive(s_lock);
    out->day = sh_clock_day(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    out->sec = (uint32_t)(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
}

esp_err_t sh_clock_set_zone(int zone)
{
    if (zone < 0 || zone >= sh_zone_count) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    use_zone(zone);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "time zone %s", sh_zones[zone].name);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_TZ, sh_zones[zone].name);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "time zone not saved: %s", esp_err_to_name(err));
    }
    return err;
}
