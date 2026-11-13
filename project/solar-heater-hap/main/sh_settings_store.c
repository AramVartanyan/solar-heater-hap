/* Created by Aram Vartanyan (c) 2026 */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sh_settings_store.h"

static const char *TAG = "sh_settings";

#define NVS_NAMESPACE   "sh_settings"
#define KEY_FROST       "fro"
#define KEY_WINTER      "ssn_now"

#ifdef CONFIG_SH_FROST_PROTECT
#define FROST_DEFAULT   true
#else
#define FROST_DEFAULT   false
#endif

_Static_assert(CONFIG_SH_PUMP_TEST_S % 30 == 0, "Control: the pump test time must be a multiple of 30 s");
_Static_assert(CONFIG_SH_DIFF_OFF_WINTER_K < CONFIG_SH_DIFF_ON_WINTER_K,
               "Control: the winter switch-off difference must be lower than the winter switch-on difference");

/* Kconfig "Control": only the defaults of the menu values */
static const sh_settings_t s_defaults = { .v = {
    [SH_SET_TANK_MAX] = CONFIG_SH_TANK_MAX_C,
    [SH_SET_COL_MIN]  = CONFIG_SH_COL_MIN_C,
    [SH_SET_DIFF_ON]  = CONFIG_SH_DIFF_ON_K,
    [SH_SET_DIFF_OFF] = CONFIG_SH_DIFF_OFF_K,
    [SH_SET_TEST_S]   = CONFIG_SH_PUMP_TEST_S,
    [SH_SET_COL_MIN_W]  = CONFIG_SH_COL_MIN_WINTER_C,
    [SH_SET_DIFF_ON_W]  = CONFIG_SH_DIFF_ON_WINTER_K,
    [SH_SET_DIFF_OFF_W] = CONFIG_SH_DIFF_OFF_WINTER_K,
    [SH_SET_SEASON]   = SH_SEASON_AUTO,
    [SH_SET_VACATION] = 0,
    [SH_SET_NCOOL]    = 1,      /* night cooling above Tmax: summer on, winter off */
    [SH_SET_NCOOL_W]  = 0,
} };

static sh_settings_t s_settings;
static bool s_frost;
static bool s_winter;           /* the current season; in the automatic mode it starts as summer */
static SemaphoreHandle_t s_lock;

esp_err_t sh_settings_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_settings = s_defaults;
    s_frost = FROST_DEFAULT;
    s_winter = false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        for (int id = 0; id < SH_SET_COUNT; id++) {
            int16_t v;
            if (nvs_get_i16(h, sh_setting_info[id].key, &v) == ESP_OK) {
                s_settings.v[id] = v;
            }
        }
        uint8_t frost;
        if (nvs_get_u8(h, KEY_FROST, &frost) == ESP_OK && frost <= 1) {
            s_frost = frost;
        }
        uint8_t winter;
        if (nvs_get_u8(h, KEY_WINTER, &winter) == ESP_OK && winter <= 1) {
            s_winter = winter;
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
    ESP_LOGI(TAG, "tbo %d degC, tSt %d s, frost protection %s; summer: C.Lo %d degC, d.on %d K, d.oF %d K, "
             "night cooling %s; winter: %d degC, %d K, %d K, %s",
             s_settings.v[SH_SET_TANK_MAX], s_settings.v[SH_SET_TEST_S], s_frost ? "on" : "off",
             s_settings.v[SH_SET_COL_MIN], s_settings.v[SH_SET_DIFF_ON], s_settings.v[SH_SET_DIFF_OFF],
             s_settings.v[SH_SET_NCOOL] ? "on" : "off",
             s_settings.v[SH_SET_COL_MIN_W], s_settings.v[SH_SET_DIFF_ON_W], s_settings.v[SH_SET_DIFF_OFF_W],
             s_settings.v[SH_SET_NCOOL_W] ? "on" : "off");
    static const char *const modes[SH_SEASON_MODES] = { "summer", "winter", "automatic" };
    ESP_LOGI(TAG, "season: %s (now %s), vacation %s", modes[s_settings.v[SH_SET_SEASON]],
             s_winter ? "winter" : "summer", s_settings.v[SH_SET_VACATION] ? "on" : "off");
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

esp_err_t sh_settings_put(const sh_settings_t *s)
{
    sh_settings_t checked = *s;
    if (sh_settings_sanitize(&checked, &s_defaults) != 0) {
        return ESP_ERR_INVALID_ARG;         /* a value out of range, or d.oF not below d.on */
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    nvs_handle_t h = 0;
    for (int id = 0; id < SH_SET_COUNT; id++) {
        if (s->v[id] == s_settings.v[id]) {
            continue;
        }
        if (!h && err == ESP_OK) {
            err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
        }
        if (err == ESP_OK) {
            err = nvs_set_i16(h, sh_setting_info[id].key, s->v[id]);
        }
        ESP_LOGI(TAG, "%s = %d", sh_setting_info[id].key, s->v[id]);
        s_settings.v[id] = s->v[id];        /* used at once, also if the write failed */
    }
    if (h) {
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings not saved: %s", esp_err_to_name(err));
    }
    return err;
}

bool sh_settings_frost(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool on = s_frost;
    xSemaphoreGive(s_lock);
    return on;
}

esp_err_t sh_settings_set_frost(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (on != s_frost) {
        nvs_handle_t h;
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_u8(h, KEY_FROST, on);
            if (err == ESP_OK) {
                err = nvs_commit(h);
            }
            nvs_close(h);
        }
        s_frost = on;
    }
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "frost protection %s", on ? "on" : "off");
    } else {
        ESP_LOGE(TAG, "frost protection %s: %s", on ? "on" : "off", esp_err_to_name(err));
    }
    return err;
}

bool sh_settings_winter(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool winter = s_winter;
    xSemaphoreGive(s_lock);
    return winter;
}

esp_err_t sh_settings_set_winter(bool winter)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (winter != s_winter) {
        nvs_handle_t h;
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_u8(h, KEY_WINTER, winter);
            if (err == ESP_OK) {
                err = nvs_commit(h);
            }
            nvs_close(h);
        }
        s_winter = winter;
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "season not saved: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t sh_settings_reset(void)
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
    s_settings = s_defaults;
    s_frost = FROST_DEFAULT;
    s_winter = false;
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGW(TAG, "user settings reset to the defaults");
    } else {
        ESP_LOGE(TAG, "user settings reset: %s", esp_err_to_name(err));
    }
    return err;
}
