/* Created by Aram Vartanyan (c) 2026 */

/* Solar water heater controller — Waveshare ESP32-S3-Zero (project solar-heater-hap)
 *
 * Boot order:
 *   1. pump relay forced off (before anything that can take time)
 *   2. NVS, shared state, user settings, installer data, time zone, history partition
 *   3. hardware: status LED, sensors, display, buttons
 *   4. measurement task, control task, history task
 *   5. the network (sh_net: captive-wifi in the mode CP, the name Solar-xxxxxx, mDNS, firmware
 *      update), SNTP (at every connection to the router) and the web interface (on the router or on
 *      the own access point); Apple HomeKit (sh_hap) is started by sh_net on the first connection
 *      to the router, if its setting is on — the web interface is then on the server of HAP
 */

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"

#include "sh_config.h"
#include "sh_state.h"
#include "sh_settings_store.h"
#include "sh_installer_store.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_sensors.h"
#include "sh_display.h"
#include "sh_buttons.h"
#include "sh_control.h"
#include "sh_web.h"
#include "sh_net.h"
#include "sh_hap.h"
#include "sh_clock.h"
#include "sh_history.h"

static const char *TAG = "app_main";

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
    ESP_ERROR_CHECK(sh_settings_init());
    ESP_ERROR_CHECK(sh_installer_init());
    ESP_ERROR_CHECK(sh_clock_init());
    ESP_ERROR_CHECK(sh_history_init());

    ESP_LOGI(TAG, "solar-heater-hap %s", esp_app_get_description()->version);

    sh_led_init();                                  /* 3. hardware */
    sh_sensors_init();
    sh_display_init();
    sh_buttons_init();

    sh_sensors_start();                             /* 4. measurement, control */
    sh_control_start();
    sh_history_start();

    ESP_ERROR_CHECK(sh_hap_init());                 /* 5. HomeKit setting, network, clock, web */
    ESP_ERROR_CHECK(sh_net_init());
    sh_clock_start();
    ESP_ERROR_CHECK(sh_web_init());
    ESP_ERROR_CHECK(sh_net_start());
}
