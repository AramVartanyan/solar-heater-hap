#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "sh_history_store.h"

static const char *TAG = "sh_history";

#define PARTITION_NAME  "history"

static SemaphoreHandle_t s_lock;    /* everything below */
static sh_hist_t s_hist;
static sh_hist_log_t s_log;
static bool s_log_ok;
static const esp_partition_t *s_part;

static bool flash_read(void *ctx, uint32_t off, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    return esp_partition_read(s_part, off, buf, len) == ESP_OK;
}

static bool flash_write(void *ctx, uint32_t off, const uint8_t *buf, uint32_t len)
{
    (void)ctx;
    return esp_partition_write(s_part, off, buf, len) == ESP_OK;
}

static bool flash_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    return esp_partition_erase_range(s_part, off, len) == ESP_OK;
}

static sh_hist_flash_t s_flash = { .read = flash_read, .write = flash_write, .erase = flash_erase };

void sh_history_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION_NAME);
    if (!s_part) {
        ESP_LOGW(TAG, "no \"%s\" partition (flash the partition table over USB): daily summaries off",
                 PARTITION_NAME);
        return;
    }
    s_flash.size = s_part->size;
    s_log_ok = sh_hist_log_open(&s_log, &s_flash);
    if (s_log_ok) {
        ESP_LOGI(TAG, "daily summaries: %d stored, room for %d", (int)s_log.count, (int)s_log.slots - 1);
    } else {
        ESP_LOGE(TAG, "\"%s\" partition not usable: daily summaries off", PARTITION_NAME);
    }
}

/* "23.4", "-0.5" for the log */
static const char *temp_str(char *buf, size_t size, int16_t t_tenths)
{
    snprintf(buf, size, "%s%d.%d", t_tenths < 0 ? "-" : "", abs(t_tenths) / 10, abs(t_tenths) % 10);
    return buf;
}

bool sh_history_available(void)
{
    return s_log_ok;
}

bool sh_history_step(const sh_hist_in_t *in)
{
    sh_hist_day_t day;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool was_reliable = s_hist.peak.reliable;
    const bool closed = sh_hist_step(&s_hist, in, &day);
    bool stored = false;
    if (closed && s_log_ok) {
        stored = sh_hist_log_append(&s_log, &day);
    }
    const bool peak_reliable = s_hist.peak.reliable;
    const bool reliable = sh_hist_reliable(&s_hist);
    xSemaphoreGive(s_lock);

    if (closed) {
        int y, m, d;
        sh_clock_date(day.day, &y, &m, &d);
        if (day.peak_min == SH_PEAK_NONE) {
            ESP_LOGI(TAG, "%04d-%02d-%02d ended: no collector value", y, m, d);
        } else {
            char lo[12], hi[12];
            ESP_LOGI(TAG, "%04d-%02d-%02d ended: collector %s ... %s degC, peak at %02d:%02d%s", y, m, d,
                     temp_str(lo, sizeof(lo), day.col_min), temp_str(hi, sizeof(hi), day.col_max),
                     day.peak_min / 60, day.peak_min % 60, day.partial ? " (not a whole day)" : "");
        }
        if (s_log_ok && !stored) {
            ESP_LOGE(TAG, "daily summary not stored");
        }
    }
    if (peak_reliable != was_reliable) {
        ESP_LOGW(TAG, "time zone check: the clock is %s", peak_reliable ? "reliable" : "NOT reliable");
    }
    return reliable;
}

void sh_history_peak_reset(bool zone_confirmed)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    sh_hist_peak_reset(&s_hist, zone_confirmed);
    xSemaphoreGive(s_lock);
}

void sh_history_today(uint32_t run_s[SH_RUN_KINDS], sh_peak_t *peak)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    sh_hist_run_today(&s_hist, run_s);
    *peak = s_hist.peak;
    xSemaphoreGive(s_lock);
}

void sh_history_chart(sh_hist_point_t *pts, int *n, bool *day, sh_hist_day_t *days, int *n_days)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *n = sh_hist_chart(&s_hist, pts, day);
    *n_days = s_log_ok ? sh_hist_log_last(&s_log, days, SH_HIST_WEB_DAYS) : 0;
    xSemaphoreGive(s_lock);
}

size_t sh_history_csv(unsigned *pos, char *buf, size_t size)
{
    size_t len = 0;
    /* the lock is held only while a part is read: the control task is not kept waiting for the
     * network */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    while (s_log_ok && *pos < s_log.count && size - len >= SH_HIST_CSV_LINE_MAX) {
        sh_hist_day_t day;
        if (sh_hist_log_get(&s_log, (uint16_t)*pos, &day)) {    /* a damaged record is left out */
            const int n = sh_hist_csv_line(buf + len, size - len, &day);
            if (n > 0) {
                len += (size_t)n;
            }
        }
        (*pos)++;
    }
    xSemaphoreGive(s_lock);
    return len;
}

esp_err_t sh_history_erase(void)
{
    if (!s_log_ok) {
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = sh_hist_log_clear(&s_log);
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "daily summaries erased%s", ok ? "" : ": FAILED");
    return ok ? ESP_OK : ESP_FAIL;
}
