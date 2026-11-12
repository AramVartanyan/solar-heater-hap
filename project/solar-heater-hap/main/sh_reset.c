#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "captive_wifi.h"
#include "sh_settings_store.h"
#include "sh_installer_store.h"
#include "sh_reset.h"

static const char *TAG = "sh_reset";

#define LATER_MS        500

void sh_reset_now(sh_reset_t what)
{
    switch (what) {
    case SH_RESET_SETTINGS:
        sh_settings_reset();        /* logs the result */
        sh_installer_reset();
        return;
    case SH_RESET_FACTORY: {
        /* Everything in the "nvs" partition: settings, Wi-Fi network (kept there by the driver)
         * and, with HomeKit, the pairings — what hap_reset_to_factory() erases too; the read-only
         * factory_nvs (setup code) stays. Wi-Fi is stopped first, so the driver does not write
         * meanwhile. */
        ESP_LOGW(TAG, "factory reset: all settings and the Wi-Fi network, restarting");
        captive_wifi_stop();
        const esp_err_t err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        }
        break;
    }
    case SH_RESET_WIFI:
    default:
        ESP_LOGW(TAG, "erasing the stored Wi-Fi network, restarting");
        captive_wifi_clear_credentials();
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(LATER_MS));
    esp_restart();
}

static void reset_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(LATER_MS));
    sh_reset_now((sh_reset_t)(intptr_t)arg);
    vTaskDelete(NULL);              /* the settings reset returns */
}

void sh_reset_later(sh_reset_t what)
{
    if (xTaskCreate(reset_task, "sh_reset", 3072, (void *)(intptr_t)what, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "reset task not created, reset now");
        sh_reset_now(what);
    }
}
