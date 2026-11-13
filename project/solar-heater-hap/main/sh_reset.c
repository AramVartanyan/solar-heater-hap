#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "captive_wifi.h"
#include "sh_hap.h"
#include "sh_reset.h"

static const char *TAG = "sh_reset";

void sh_reset_now(sh_reset_t what)
{
    if (what == SH_RESET_FACTORY && sh_hap_reset_to_factory()) {
        /* HAP runs: hap_reset_to_factory() closes the sessions, ends the mDNS announcement, erases
         * the "nvs" partition and restarts */
        ESP_LOGW(TAG, "factory reset through HomeKit (all settings, pairings and the Wi-Fi network)");
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    if (what == SH_RESET_RESTART) {
        ESP_LOGW(TAG, "restarting");
    } else if (what == SH_RESET_FACTORY) {
        /* Everything in the "nvs" partition: settings, Wi-Fi network (kept there by the driver)
         * and, with HomeKit, the pairings — what hap_reset_to_factory() erases too; the read-only
         * factory_nvs (setup code) stays. Wi-Fi is stopped first, so the driver does not write
         * meanwhile. */
        ESP_LOGW(TAG, "factory reset (all settings and the Wi-Fi network), restarting");
        captive_wifi_stop();
        esp_err_t err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "erasing the stored Wi-Fi network, restarting");
        captive_wifi_clear_credentials();
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void reset_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    sh_reset_now((sh_reset_t)(intptr_t)arg);
}

esp_err_t sh_reset_later(sh_reset_t what)
{
    if (xTaskCreate(reset_task, "sh_reset", 4096, (void *)(intptr_t)what, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
