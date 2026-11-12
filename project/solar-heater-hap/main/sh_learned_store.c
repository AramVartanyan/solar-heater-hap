#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sh_learned_store.h"

static const char *TAG = "sh_learned";

#define NVS_NAMESPACE   "sh_learned"
#define KEY_VALUES      "v"
#define VERSION         1

/* The blob: version, then the struct as it is (the same firmware writes and reads it). */
typedef struct {
    uint8_t version;
    sh_learned_t l;
} blob_t;

static bool list_ok(const sh_sun_list_t *s)
{
    if (s->n > SH_SUN_DAYS) {
        return false;
    }
    for (int i = 0; i < s->n; i++) {
        if (s->v[i] < 0 || s->v[i] >= 1440) {
            return false;
        }
    }
    return true;
}

esp_err_t sh_learned_load(sh_learned_t *out)
{
    sh_learned_init(out);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;                  /* nothing learned yet */
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open: %s", esp_err_to_name(err));
        return err;
    }
    blob_t b;
    size_t len = sizeof(b);
    err = nvs_get_blob(h, KEY_VALUES, &b, &len);
    nvs_close(h);
    if (err == ESP_OK && len == sizeof(b) && b.version == VERSION && list_ok(&b.l.rise) && list_ok(&b.l.set) &&
        b.l.cool.offset <= 0 && b.l.cool.offset >= SH_COOL_LEARN_MIN && b.l.cool.ok_days < SH_COOL_LEARN_DAYS) {
        *out = b.l;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "stored values invalid, none used");
    }
    ESP_LOGI(TAG, "sunrise %d days, sunset %d days, cooling target correction %d.%d K", out->rise.n, out->set.n,
             out->cool.offset / 10, -out->cool.offset % 10);
    return ESP_OK;
}

esp_err_t sh_learned_save(const sh_learned_t *l)
{
    blob_t b;
    memset(&b, 0, sizeof(b));
    b.version = VERSION;
    b.l = *l;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_VALUES, &b, sizeof(b));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "not saved: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t sh_learned_erase(void)
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
    return err;
}
