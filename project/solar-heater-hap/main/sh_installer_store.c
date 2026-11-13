/* Created by Aram Vartanyan (c) 2026 */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sh_installer_store.h"

static const char *TAG = "sh_installer";

#define NVS_NAMESPACE   "sh_installer"

/* Kconfig "Installer data": only the defaults */
static const sh_installer_t s_defaults = { .v = {
#ifdef CONFIG_SH_INST_GLYCOL
    [SH_INST_FLUID]    = SH_FLUID_GLYCOL,
    [SH_INST_GLYCOL]   = CONFIG_SH_INST_GLYCOL_PCT,
#else
    [SH_INST_FLUID]    = SH_FLUID_WATER,
    [SH_INST_GLYCOL]   = 0,
#endif
    [SH_INST_PRESSURE] = CONFIG_SH_INST_PRESSURE_DBAR,
    [SH_INST_COL_MAX]  = CONFIG_SH_INST_COL_MAX_C,
#ifdef CONFIG_SH_INST_COL_TUBES
    [SH_INST_COL_TYPE] = SH_COL_TUBES,
#else
    [SH_INST_COL_TYPE] = SH_COL_FLAT,
#endif
    [SH_INST_COL_AREA] = CONFIG_SH_INST_COL_AREA_DM2,
    [SH_INST_TANK_VOL] = CONFIG_SH_INST_TANK_L,
    [SH_INST_HEATER]   = CONFIG_SH_INST_HEATER_DKW,
#ifdef CONFIG_SH_INST_MOUNT_PIPES
    [SH_INST_MOUNT]    = SH_MOUNT_PIPES,
#else
    [SH_INST_MOUNT]    = SH_MOUNT_SLEEVES,
#endif
} };

_Static_assert(CONFIG_SH_INST_TANK_L % 10 == 0, "Installer data: the tank volume must be a multiple of 10 l");
_Static_assert(CONFIG_SH_INST_HEATER_DKW % 5 == 0, "Installer data: the heater power must be a multiple of 0.5 kW");

static sh_installer_t s_data;
static sh_limits_t s_limits;
static SemaphoreHandle_t s_lock;

/* Called with the lock held (or before the tasks start). */
static void use(const sh_installer_t *d)
{
    s_data = *d;
    sh_limits_calc(&s_data, &s_limits);
    ESP_LOGI(TAG, "%s, glycol %d %%, minimum pressure %d.%d bar, collector maximum %d degC; "
             "collector: %s, %d.%d m2; tank %d l, heater %d.%d kW; sensors %s",
             s_data.v[SH_INST_FLUID] == SH_FLUID_GLYCOL ? "propylene glycol" : "water",
             s_data.v[SH_INST_GLYCOL], s_data.v[SH_INST_PRESSURE] / 10, s_data.v[SH_INST_PRESSURE] % 10,
             s_data.v[SH_INST_COL_MAX],
             s_data.v[SH_INST_COL_TYPE] == SH_COL_TUBES ? "evacuated tubes" : "flat",
             s_data.v[SH_INST_COL_AREA] / 10, s_data.v[SH_INST_COL_AREA] % 10, s_data.v[SH_INST_TANK_VOL],
             s_data.v[SH_INST_HEATER] / 10, s_data.v[SH_INST_HEATER] % 10,
             s_data.v[SH_INST_MOUNT] == SH_MOUNT_PIPES ? "on the pipes (pulse mode)" : "in the sleeves");
}

esp_err_t sh_installer_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    sh_installer_t d = s_defaults;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        for (int id = 0; id < SH_INST_COUNT; id++) {
            int16_t v;
            if (nvs_get_i16(h, sh_inst_info[id].key, &v) == ESP_OK) {
                d.v[id] = v;
            }
        }
        nvs_close(h);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {      /* not found: nothing stored yet */
        ESP_LOGE(TAG, "NVS open: %s, defaults used", esp_err_to_name(err));
    }

    unsigned replaced = sh_installer_sanitize(&d, &s_defaults);
    for (int id = 0; id < SH_INST_COUNT; id++) {
        if (replaced & (1u << id)) {
            ESP_LOGW(TAG, "%s: stored value invalid, default used", sh_inst_info[id].key);
        }
    }
    use(&d);
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

esp_err_t sh_installer_set(const sh_installer_t *data)
{
    if (!sh_installer_ok(data)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    nvs_handle_t h = 0;
    bool changed = false;
    for (int id = 0; id < SH_INST_COUNT; id++) {
        if (data->v[id] == s_data.v[id]) {
            continue;
        }
        changed = true;
        if (!h && err == ESP_OK) {
            err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
        }
        if (err == ESP_OK) {
            err = nvs_set_i16(h, sh_inst_info[id].key, data->v[id]);
        }
    }
    if (h) {
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (changed) {
        use(data);                          /* used at once, also if the write failed */
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "installer data not saved: %s", esp_err_to_name(err));
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
    ESP_LOGW(TAG, "installer data reset to the defaults");
    use(&s_defaults);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "installer data reset: %s", esp_err_to_name(err));
    }
    return err;
}
