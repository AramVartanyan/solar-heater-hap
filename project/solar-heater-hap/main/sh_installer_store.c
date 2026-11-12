#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sh_installer_store.h"

static const char *TAG = "sh_installer";

#define NVS_NAMESPACE   "sh_installer"

#ifdef CONFIG_SH_COOLANT_GLYCOL
#define COOLANT_DEFAULT SH_COOLANT_GLYCOL
#else
#define COOLANT_DEFAULT SH_COOLANT_WATER
#endif

#if !defined(CONFIG_SH_COOLANT_GLYCOL) && CONFIG_SH_GLYCOL_PCT != 0
#error "Installer data: the glycol share must be 0 with water"
#endif

/* Kconfig "Installer data": the defaults */
static const sh_installer_t s_defaults = { .v = {
    [SH_INST_COOLANT] = COOLANT_DEFAULT,
    [SH_INST_GLYCOL]  = CONFIG_SH_GLYCOL_PCT,
    [SH_INST_P_MIN]   = CONFIG_SH_P_MIN_DBAR,
    [SH_INST_COL_MAX] = CONFIG_SH_COL_MAX_C,
} };

static sh_installer_t s_data;
static sh_limits_t s_limits;
static SemaphoreHandle_t s_lock;

int16_t sh_installer_col_protect(void)
{
    return CONFIG_SH_COL_PROTECT_C * 10;
}

int16_t sh_installer_col_protect_used(const sh_limits_t *limits)
{
    const int16_t set = sh_installer_col_protect();
    return set < limits->protect_cap ? set : limits->protect_cap;
}

/* "127.7" */
static const char *tenths(char *buf, size_t size, int16_t t)
{
    snprintf(buf, size, "%s%d.%d", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
    return buf;
}

static void log_data(const sh_installer_t *d, const sh_limits_t *l)
{
    char boil[12], ban_off[12], cap[12], frost[12];
    ESP_LOGI(TAG, "%s %d %%, minimum pressure %d.%d bar, collector max %d degC",
             d->v[SH_INST_COOLANT] == SH_COOLANT_GLYCOL ? "propylene glycol" : "water",
             d->v[SH_INST_GLYCOL], d->v[SH_INST_P_MIN] / 10, d->v[SH_INST_P_MIN] % 10,
             d->v[SH_INST_COL_MAX]);
    ESP_LOGI(TAG, "boiling point %s degC: start ban (released below %s degC); "
             "collector protection cap %s degC; frost protection below %s degC",
             tenths(boil, sizeof(boil), l->boiling),
             tenths(ban_off, sizeof(ban_off), (int16_t)(l->boiling - 20)),
             tenths(cap, sizeof(cap), l->protect_cap), tenths(frost, sizeof(frost), l->frost));
    const int16_t set = sh_installer_col_protect();
    if (set > l->protect_cap) {
        ESP_LOGW(TAG, "collector protection %d degC is limited to %s degC", set / 10, cap);
    } else {
        ESP_LOGI(TAG, "collector protection %d degC", set / 10);
    }
}

esp_err_t sh_installer_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_data = s_defaults;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        for (int id = 0; id < SH_INST_COUNT; id++) {
            int16_t v;
            if (nvs_get_i16(h, sh_inst_info[id].key, &v) == ESP_OK) {
                s_data.v[id] = v;
            }
        }
        nvs_close(h);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {      /* not found: nothing stored yet */
        ESP_LOGE(TAG, "NVS open: %s, defaults used", esp_err_to_name(err));
    }

    const unsigned replaced = sh_installer_sanitize(&s_data, &s_defaults);
    for (int id = 0; id < SH_INST_COUNT; id++) {
        if (replaced & (1u << id)) {
            ESP_LOGW(TAG, "%s: stored value invalid, default used", sh_inst_info[id].key);
        }
    }
    sh_installer_limits(&s_data, &s_limits);
    log_data(&s_data, &s_limits);
    return ESP_OK;
}

void sh_installer_get(sh_installer_t *data, sh_limits_t *limits)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (data) {
        *data = s_data;
    }
    if (limits) {
        *limits = s_limits;
    }
    xSemaphoreGive(s_lock);
}

esp_err_t sh_installer_set(sh_inst_id_t id, int16_t value)
{
    if ((unsigned)id >= SH_INST_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    sh_installer_t next = s_data;
    esp_err_t err = ESP_OK;
    bool changed = false;
    if (!sh_installer_apply(&next, id, value)) {
        err = ESP_ERR_INVALID_ARG;
    } else {
        for (int i = 0; i < SH_INST_COUNT; i++) {
            changed = changed || next.v[i] != s_data.v[i];
        }
    }
    if (changed) {
        /* the value and, with water, the glycol share it may have cleared */
        nvs_handle_t h;
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            for (int i = 0; i < SH_INST_COUNT && err == ESP_OK; i++) {
                if (next.v[i] != s_data.v[i]) {
                    err = nvs_set_i16(h, sh_inst_info[i].key, next.v[i]);
                }
            }
            if (err == ESP_OK) {
                err = nvs_commit(h);
            }
            nvs_close(h);
        }
        /* used at once, also if the write failed (then until the next restart) */
        s_data = next;
        sh_installer_limits(&s_data, &s_limits);
    }
    const sh_installer_t data = s_data;
    const sh_limits_t limits = s_limits;
    xSemaphoreGive(s_lock);

    if (err == ESP_OK && changed) {
        ESP_LOGI(TAG, "%s = %d saved", sh_inst_info[id].key, value);
        log_data(&data, &limits);
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s = %d: %s", sh_inst_info[id].key, value, esp_err_to_name(err));
        if (changed) {
            log_data(&data, &limits);
        }
    }
    return err;
}

esp_err_t sh_installer_reset(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_all(h);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    s_data = s_defaults;
    sh_installer_limits(&s_data, &s_limits);
    const sh_installer_t data = s_data;
    const sh_limits_t limits = s_limits;
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGW(TAG, "installer data reset to the defaults");
    } else {
        ESP_LOGE(TAG, "installer data reset: %s", esp_err_to_name(err));
    }
    log_data(&data, &limits);
    return err;
}
