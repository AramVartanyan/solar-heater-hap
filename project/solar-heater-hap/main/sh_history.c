#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "sh_state.h"
#include "sh_clock.h"
#include "sh_history.h"

static const char *TAG = "sh_history";

#define PERIOD_MS       1000
#define PARTITION       "history"

static SemaphoreHandle_t s_day_lock;    /* the day in RAM */
static SemaphoreHandle_t s_hist_lock;   /* the records in the flash */
static sh_day_t s_day;
static const esp_partition_t *s_part;
static sh_hist_flash_t s_flash;
static sh_hist_t s_hist;
static bool s_on;

static bool part_read(void *ctx, uint32_t off, void *buf, uint32_t len)
{
    return esp_partition_read(ctx, off, buf, len) == ESP_OK;
}

static bool part_write(void *ctx, uint32_t off, const void *buf, uint32_t len)
{
    return esp_partition_write(ctx, off, buf, len) == ESP_OK;
}

static bool part_erase(void *ctx, uint32_t off, uint32_t len)
{
    return esp_partition_erase_range(ctx, off, len) == ESP_OK;
}

static bool count_one(void *ctx, const sh_hist_rec_t *r)
{
    (void)r;
    (void)ctx;
    return true;
}

esp_err_t sh_history_init(void)
{
    s_day_lock = xSemaphoreCreateMutex();
    s_hist_lock = xSemaphoreCreateMutex();
    if (!s_day_lock || !s_hist_lock) {
        return ESP_ERR_NO_MEM;
    }
    sh_day_init(&s_day);

    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION);
    if (!s_part) {
        ESP_LOGW(TAG, "no \"%s\" partition: history off (flash the new partition table over USB)", PARTITION);
        return ESP_OK;
    }
    s_flash = (sh_hist_flash_t){
        .read = part_read, .write = part_write, .erase = part_erase, .ctx = (void *)s_part,
        .size = s_part->size, .sector = s_part->erase_size,
    };
    if (!sh_hist_open(&s_hist, &s_flash)) {
        ESP_LOGE(TAG, "history partition: read error, history off");
        return ESP_OK;
    }
    s_on = true;
    const unsigned n = sh_hist_walk(&s_hist, true, 0, count_one, NULL);
    ESP_LOGI(TAG, "history: %u days stored (room for %lu)", n,
             (unsigned long)(s_hist.slots - s_flash.sector / SH_HIST_REC_SIZE));
    return ESP_OK;
}

static uint16_t minutes(uint32_t s)
{
    const uint32_t m = (s + 30) / 60;
    return (uint16_t)(m < 0xFFFF ? m : 0xFFFF);
}

static int16_t hist_temp(int16_t t)
{
    return t == SH_TEMP_INVALID ? SH_HIST_NO_TEMP : t;
}

static void day_over(const sh_day_sum_t *d, const sh_clock_info_t *ci, int16_t cool_target)
{
    sh_hist_rec_t r;
    sh_hist_rec_init(&r, d->day);
    r.col_max = hist_temp(d->col_max);
    r.col_min = hist_temp(d->col_min);
    r.tank_max = hist_temp(d->tank_max);
    r.tank_min = hist_temp(d->tank_min);
    r.run_min[0] = minutes(d->run_s[SH_RUN_CHARGE]);
    r.run_min[1] = minutes(d->run_s[SH_RUN_PROTECT]);
    r.run_min[2] = minutes(d->run_s[SH_RUN_OTHER]);
    r.cool_min = minutes(d->run_s[SH_RUN_COOL]);
    /* the learned values: the sunrise and sunset detected that day, the cooling target (whole °C) */
    r.sunrise = d->sunrise >= 0 ? (uint16_t)d->sunrise : SH_HIST_NO_TIME;
    r.sunset = d->sunset >= 0 ? (uint16_t)d->sunset : SH_HIST_NO_TIME;
    r.cool_target = (uint8_t)((cool_target + 5) / 10);
    r.alarms = d->alarms;
    r.peak_min = d->peak_min;
    r.clock_reliable = ci->trust == SH_CLOCK_RELIABLE;
    r.partial = !d->full;

    if (s_on) {
        xSemaphoreTake(s_hist_lock, portMAX_DELAY);
        const bool ok = sh_hist_append(&s_hist, &r);
        xSemaphoreGive(s_hist_lock);
        int y, m, md;
        sh_day_date(d->day, &y, &m, &md);
        if (ok) {
            ESP_LOGI(TAG, "day %04d-%02d-%02d saved%s: pump %u / %u / %u min, cooling %u min", y, m, md,
                     r.partial ? " (partial)" : "", r.run_min[0], r.run_min[1], r.run_min[2], r.cool_min);
        } else {
            ESP_LOGE(TAG, "day %04d-%02d-%02d: flash write error", y, m, md);
        }
    }
    sh_clock_day(d);
}

static void history_task(void *arg)
{
    (void)arg;
    TickType_t next = xTaskGetTickCount();
    for (;;) {
        sh_state_t st;
        sh_state_get(&st);
        sh_clock_info_t ci;
        sh_clock_get(&ci);
        const sh_day_in_t in = {
            .t_col = st.t_col, .t_tank = st.t_tank, .col_fault = st.col_fault, .tank_fault = st.tank_fault,
            .pump_on = st.pump_on, .reason = st.pump_reason, .alarms = st.alarms,
            .sunrises = st.modes.sunrises, .sunsets = st.modes.sunsets,
        };
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
        sh_day_sum_t done;
        xSemaphoreTake(s_day_lock, portMAX_DELAY);
        const bool over = sh_day_step(&s_day, &in, &ci.lt, now, &done);
        xSemaphoreGive(s_day_lock);
        if (over) {
            day_over(&done, &ci, st.modes.cool_learned);
        }
        vTaskDelayUntil(&next, pdMS_TO_TICKS(PERIOD_MS));
    }
}

esp_err_t sh_history_start(void)
{
    if (xTaskCreate(history_task, "sh_history", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "history task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool sh_history_on(void)
{
    return s_on;
}

const sh_day_t *sh_history_day_lock(void)
{
    xSemaphoreTake(s_day_lock, portMAX_DELAY);
    return &s_day;
}

void sh_history_day_unlock(void)
{
    xSemaphoreGive(s_day_lock);
}

unsigned sh_history_walk(bool newest_first, unsigned max, sh_hist_fn fn, void *ctx)
{
    if (!s_on) {
        return 0;
    }
    xSemaphoreTake(s_hist_lock, portMAX_DELAY);
    const unsigned n = sh_hist_walk(&s_hist, newest_first, max, fn, ctx);
    xSemaphoreGive(s_hist_lock);
    return n;
}

esp_err_t sh_history_clear(void)
{
    if (!s_on) {
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreTake(s_hist_lock, portMAX_DELAY);
    const bool ok = sh_hist_clear(&s_hist);
    xSemaphoreGive(s_hist_lock);
    ESP_LOGW(TAG, "history erased%s", ok ? "" : ": flash error");
    return ok ? ESP_OK : ESP_FAIL;
}
