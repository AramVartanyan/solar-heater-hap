#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sh_settings.h"

static const char *TAG = "sh_settings";

#define NVS_NAMESPACE   "sh_settings"

_Static_assert(CONFIG_SH_DIFF_OFF_K < CONFIG_SH_DIFF_ON_K,
               "Control: the switch-off difference must be lower than the switch-on difference");

static const sh_settings_t s_defaults = {
    .v = {
        [SH_SET_TANK_MAX] = CONFIG_SH_TANK_MAX_C,
        [SH_SET_COL_MIN]  = CONFIG_SH_COL_MIN_C,
        [SH_SET_DIFF_ON]  = CONFIG_SH_DIFF_ON_K,
        [SH_SET_DIFF_OFF] = CONFIG_SH_DIFF_OFF_K,
        [SH_SET_TEST_S]   = CONFIG_SH_PUMP_TEST_S,
    },
};

/* NVS keys; the names on the display are in sh_menu.c */
static const char *const s_key[SH_SET_COUNT] = {
    [SH_SET_TANK_MAX] = "tbo",
    [SH_SET_COL_MIN]  = "c_lo",
    [SH_SET_DIFF_ON]  = "d_on",
    [SH_SET_DIFF_OFF] = "d_of",
    [SH_SET_TEST_S]   = "tst",
};

static sh_settings_t s_settings;
static SemaphoreHandle_t s_lock;

esp_err_t sh_settings_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    sh_settings_t stored = { 0 };
    uint32_t present = 0;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        for (int id = 0; id < SH_SET_COUNT; id++) {
            if (nvs_get_i16(nvs, s_key[id], &stored.v[id]) == ESP_OK) {
                present |= 1u << id;
            }
        }
        nvs_close(nvs);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {      /* not found: nothing stored yet */
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    }

    sh_settings_check(&s_settings, &stored, present, &s_defaults);
    for (int id = 0; id < SH_SET_COUNT; id++) {
        if ((present & (1u << id)) && s_settings.v[id] != stored.v[id]) {
            ESP_LOGW(TAG, "%s: stored %d is not valid, default %d in use", s_key[id], stored.v[id],
                     s_settings.v[id]);
        }
    }
    ESP_LOGI(TAG, "Tmax %d °C, collector minimum %d °C, difference on %d K / off %d K, pump test %d s",
             s_settings.v[SH_SET_TANK_MAX], s_settings.v[SH_SET_COL_MIN], s_settings.v[SH_SET_DIFF_ON],
             s_settings.v[SH_SET_DIFF_OFF], s_settings.v[SH_SET_TEST_S]);
    return ESP_OK;
}

void sh_settings_get(sh_settings_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_settings;
    xSemaphoreGive(s_lock);
}

esp_err_t sh_settings_set(sh_setting_id_t id, int16_t value)
{
    if ((unsigned)id >= SH_SET_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const sh_setting_range_t r = sh_setting_range(&s_settings, id);
    const bool valid = value >= r.min && value <= r.max;
    const bool changed = valid && value != s_settings.v[id];
    if (changed) {
        s_settings.v[id] = value;
    }
    xSemaphoreGive(s_lock);
    if (!valid) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!changed) {
        return ESP_OK;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_i16(nvs, s_key[id], value);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s = %d stored", s_key[id], value);
    } else {
        ESP_LOGE(TAG, "%s = %d in use, but not stored: %s", s_key[id], value, esp_err_to_name(err));
    }
    return err;
}
