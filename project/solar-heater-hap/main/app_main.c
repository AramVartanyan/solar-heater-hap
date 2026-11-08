/* Solar water heater controller — Waveshare ESP32-S3-Zero (project solar-heater-hap)
 *
 * Boot order:
 *   1. pump relay forced off (before anything that can take time)
 *   2. NVS, shared state
 *   3. hardware: status LED, sensors, display, buttons
 *   4. control task
 *   5. Wi-Fi (captive-wifi); HomeKit follows in a later phase
 */

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"

#include "captive_wifi.h"

#include "sh_config.h"
#include "sh_state.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_sensors.h"
#include "sh_display.h"
#include "sh_buttons.h"
#include "sh_control.h"

static const char *TAG = "app_main";

static void wifi_event(captive_wifi_event_t evt, void *ctx)
{
    (void)ctx;
    switch (evt) {
    case CAPTIVE_WIFI_EVT_STA_CONNECTED:
        sh_state_set_wifi(true, false);
        break;
    case CAPTIVE_WIFI_EVT_STA_DISCONNECTED:
        sh_state_set_wifi(false, captive_wifi_portal_active());
        break;
    case CAPTIVE_WIFI_EVT_PORTAL_STARTED:
        ESP_LOGI(TAG, "Wi-Fi setup: join %s, open http://%s", captive_wifi_ap_ssid(),
                 captive_wifi_portal_ip());
        sh_state_set_wifi(false, true);
        break;
    case CAPTIVE_WIFI_EVT_PORTAL_STOPPED:
        sh_state_set_wifi(captive_wifi_is_connected(), false);
        break;
    default:
        break;
    }
}

void app_main(void)
{
    sh_relay_init();                                /* 1. pump off */

    esp_err_t err = nvs_flash_init();               /* 2. NVS */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    sh_state_init();

    ESP_LOGI(TAG, "solar-heater-hap %s", esp_app_get_description()->version);

    sh_led_init();                                  /* 3. hardware */
    sh_sensors_init();
    sh_display_init();
    sh_buttons_init();

    sh_control_start();                             /* 4. control */

    ESP_ERROR_CHECK(captive_wifi_init(SH_HOSTNAME, wifi_event, NULL));   /* 5. Wi-Fi */
    ESP_ERROR_CHECK(captive_wifi_start());
}
