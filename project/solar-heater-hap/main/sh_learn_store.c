/* Created by Aram Vartanyan (c) 2026 */

#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sh_cool.h"
#include "sh_learn_store.h"

static const char *TAG = "sh_learn";

#define NVS_NAMESPACE   "sh_learn"
#define KEY_SUN         "sun"           /* blob: 7 sunrises, 7 sunsets (u16 LE, local minutes) */
#define KEY_COOL        "cool"          /* blob: offset (i8), quiet mornings (u8) */
#define KEY_HEATER      "heater"        /* u8: 1 = detected with "0 kW" */
#define KEY_E8_RATE     "e8rate"        /* blob: 7 normal cooling rates (i16 LE, 0.1 K/h) */
#define KEY_E8          "e8"            /* blob: nights with the sign, normal nights (u8), E8 (u8) */
#define SUN_SIZE        (4 * SH_SUN_DAYS)
#define RATE_SIZE       (2 * SH_E8_DAYS)

void sh_learn_load(sh_learned_t *out)
{
    sh_sun_days_reset(&out->days);
    out->cool_offset = 0;
    out->cool_quiet = 0;
    out->heater = false;
    sh_e8_learned_reset(&out->e8);

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
    uint8_t heater;
    if (nvs_get_u8(h, KEY_HEATER, &heater) == ESP_OK) {
        out->heater = heater == 1;
    }
    uint8_t rate[RATE_SIZE];
    len = sizeof(rate);
    if (nvs_get_blob(h, KEY_E8_RATE, rate, &len) == ESP_OK && len == sizeof(rate)) {
        for (int i = 0; i < SH_E8_DAYS; i++) {
            out->e8.rate[i] = (int16_t)(rate[2 * i] | rate[2 * i + 1] << 8);
        }
    }
    uint8_t e8[3];
    len = sizeof(e8);
    if (nvs_get_blob(h, KEY_E8, e8, &len) == ESP_OK && len == sizeof(e8) && e8[2] <= 1) {
        out->e8.sign_nights = e8[0];
        out->e8.normal_nights = e8[1];
        out->e8.active = e8[2] == 1;
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
    uint8_t rate[RATE_SIZE];
    for (int i = 0; i < SH_E8_DAYS; i++) {
        rate[2 * i] = (uint8_t)l->e8.rate[i];
        rate[2 * i + 1] = (uint8_t)((uint16_t)l->e8.rate[i] >> 8);
    }
    const uint8_t e8[3] = { l->e8.sign_nights, l->e8.normal_nights, l->e8.active };

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_SUN, sun, sizeof(sun));       /* NVS skips a value that did not change */
        if (err == ESP_OK) {
            err = nvs_set_blob(h, KEY_COOL, cool, sizeof(cool));
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(h, KEY_HEATER, l->heater);
        }
        if (err == ESP_OK) {
            err = nvs_set_blob(h, KEY_E8_RATE, rate, sizeof(rate));
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
        /* everything but the E8 counters */
        static const char *const keys[] = { KEY_SUN, KEY_COOL, KEY_HEATER, KEY_E8_RATE };
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]) && err == ESP_OK; i++) {
            err = nvs_erase_key(h, keys[i]);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;                   /* not stored yet */
            }
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "learned values reset: %s", esp_err_to_name(err));
    }
    return err;
}
