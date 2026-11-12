#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sh_cool.h"
#include "sh_learn_store.h"

static const char *TAG = "sh_learn";

#define NVS_NAMESPACE   "sh_learn"
#define KEY_SUN         "sun"           /* blob: 7 sunrises, 7 sunsets (u16 LE, local minutes) */
#define KEY_COOL        "cool"          /* blob: offset (i8), quiet mornings (u8) */
#define KEY_HEATER      "heater"        /* u8: a heater was found with the setting "0 kW" */
#define KEY_E8          "e8"            /* blob: E8 (u8), nights with the sign, normal nights, 7 rates (u8) */
#define SUN_SIZE        (4 * SH_SUN_DAYS)
#define E8_SIZE         (3 + SH_E8_LEARN_NIGHTS)

void sh_learn_load(sh_learned_t *out)
{
    sh_sun_days_reset(&out->days);
    out->cool_offset = 0;
    out->cool_quiet = 0;
    out->heater_found = false;
    sh_e8_keep_reset(&out->e8);

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;                         /* nothing stored yet */
    }
    uint8_t sun[SUN_SIZE];
    size_t len = sizeof(sun);
    if (nvs_get_blob(h, KEY_SUN, sun, &len) == ESP_OK && len == sizeof(sun)) {
        for (int i = 0; i < SH_SUN_DAYS; i++) {
            const uint16_t rise = (uint16_t)(sun[2 * i] | sun[2 * i + 1] << 8);
            const uint16_t set = (uint16_t)(sun[2 * (SH_SUN_DAYS + i)] | sun[2 * (SH_SUN_DAYS + i) + 1] << 8);
            out->days.rise[i] = rise < SH_DAY_MIN ? rise : SH_SUN_NONE;
            out->days.set[i] = set < SH_DAY_MIN ? set : SH_SUN_NONE;
        }
    }
    uint8_t cool[2];
    len = sizeof(cool);
    if (nvs_get_blob(h, KEY_COOL, cool, &len) == ESP_OK && len == sizeof(cool) &&
        (int8_t)cool[0] <= 0 && (int8_t)cool[0] >= SH_COOL_OFFSET_MIN && cool[1] < SH_COOL_QUIET_DAYS) {
        out->cool_offset = (int8_t)cool[0];
        out->cool_quiet = cool[1];
    }
    uint8_t found;
    if (nvs_get_u8(h, KEY_HEATER, &found) == ESP_OK) {
        out->heater_found = found == 1;
    }
    uint8_t e8[E8_SIZE];
    len = sizeof(e8);
    if (nvs_get_blob(h, KEY_E8, e8, &len) == ESP_OK && len == sizeof(e8) && e8[0] <= 1 &&
        e8[1] <= SH_E8_NIGHTS && e8[2] <= SH_E8_NIGHTS) {
        out->e8.active = e8[0] != 0;
        out->e8.sign_nights = e8[1];
        out->e8.normal_nights = e8[2];
        for (int i = 0; i < SH_E8_LEARN_NIGHTS; i++) {
            out->e8.rate[i] = e8[3 + i] <= SH_E8_RATE_MAX ? e8[3 + i] : SH_E8_NO_RATE;
        }
    }
    nvs_close(h);
}

esp_err_t sh_learn_save(const sh_learned_t *l)
{
    uint8_t sun[SUN_SIZE];
    for (int i = 0; i < SH_SUN_DAYS; i++) {
        sun[2 * i] = (uint8_t)l->days.rise[i];
        sun[2 * i + 1] = (uint8_t)(l->days.rise[i] >> 8);
        sun[2 * (SH_SUN_DAYS + i)] = (uint8_t)l->days.set[i];
        sun[2 * (SH_SUN_DAYS + i) + 1] = (uint8_t)(l->days.set[i] >> 8);
    }
    const uint8_t cool[2] = { (uint8_t)l->cool_offset, l->cool_quiet };
    uint8_t e8[E8_SIZE] = { l->e8.active, l->e8.sign_nights, l->e8.normal_nights };
    for (int i = 0; i < SH_E8_LEARN_NIGHTS; i++) {
        e8[3 + i] = l->e8.rate[i];
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_SUN, sun, sizeof(sun));       /* NVS skips a value that did not change */
        if (err == ESP_OK) {
            err = nvs_set_blob(h, KEY_COOL, cool, sizeof(cool));
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(h, KEY_HEATER, l->heater_found);
        }
        if (err == ESP_OK) {
            err = nvs_set_blob(h, KEY_E8, e8, sizeof(e8));
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "learned values not saved: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t sh_learn_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_all(h);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "learned values reset: %s", esp_err_to_name(err));
    }
    return err;
}
