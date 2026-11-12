#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sh_settings_store.h"

static const char *TAG = "sh_settings";

#define NVS_NAMESPACE   "sh_settings"

_Static_assert(CONFIG_SH_PUMP_TEST_S % 30 == 0, "Control: the pump test time must be a multiple of 30 s");

/* Kconfig "Control": only the defaults of the menu values */
static const sh_settings_t s_defaults = { .v = {
    [SH_SET_TANK_MAX] = CONFIG_SH_TANK_MAX_C,
    [SH_SET_COL_MIN]  = CONFIG_SH_COL_MIN_C,
    [SH_SET_DIFF_ON]  = CONFIG_SH_DIFF_ON_K,
    [SH_SET_DIFF_OFF] = CONFIG_SH_DIFF_OFF_K,
    [SH_SET_TEST_S]   = CONFIG_SH_PUMP_TEST_S,
} };

static sh_settings_t s_settings;
static SemaphoreHandle_t s_lock;

esp_err_t sh_settings_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_settings = s_defaults;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        for (int id = 0; id < SH_SET_COUNT; id++) {
            int16_t v;
            if (nvs_get_i16(h, sh_setting_info[id].key, &v) == ESP_OK) {
                s_settings.v[id] = v;
            }
        }
        nvs_close(h);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {      /* not found: nothing stored yet */
        ESP_LOGE(TAG, "NVS open: %s, defaults used", esp_err_to_name(err));
    }

    unsigned replaced = sh_settings_sanitize(&s_settings, &s_defaults);
    for (int id = 0; id < SH_SET_COUNT; id++) {
        if (replaced & (1u << id)) {
            ESP_LOGW(TAG, "%s: stored value invalid, default used", sh_setting_info[id].key);
        }
    }
    ESP_LOGI(TAG, "tbo %d degC, C.Lo %d degC, d.on %d K, d.oF %d K, tSt %d s",
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

esp_err_t sh_settings_set(sh_setting_t id, int16_t value)
{
    if ((unsigned)id >= SH_SET_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int16_t min, max;
    sh_settings_range(&s_settings, id, &min, &max);
    esp_err_t err = ESP_OK;
    if (!sh_settings_value_ok(id, value) || value < min || value > max) {
        err = ESP_ERR_INVALID_ARG;
    } else if (value != s_settings.v[id]) {
        nvs_handle_t h;
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_i16(h, sh_setting_info[id].key, value);
            if (err == ESP_OK) {
                err = nvs_commit(h);
            }
            nvs_close(h);
        }
        /* used at once, also if the write failed (then until the next restart) */
        s_settings.v[id] = value;
    }
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s = %d saved", sh_setting_info[id].key, value);
    } else {
        ESP_LOGE(TAG, "%s = %d: %s", sh_setting_info[id].key, value, esp_err_to_name(err));
    }
    return err;
}
