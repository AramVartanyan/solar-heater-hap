#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "iot_button.h"
#include "captive_wifi.h"
#include "sh_config.h"
#include "sh_buttons.h"

static const char *TAG = "sh_buttons";

#if CONFIG_SH_FACTORY_RESET_HOLD_S <= CONFIG_SH_NET_RESET_HOLD_S
#error "SH_FACTORY_RESET_HOLD_S must be longer than SH_NET_RESET_HOLD_S"
#endif

typedef enum { RESET_NETWORK, RESET_FACTORY } reset_t;

static void on_tap(void *arg)
{
    ESP_LOGI(TAG, "button %s", (const char *)arg);   /* menu logic comes later */
}

/* Erasing NVS blocks, so it runs in its own short task, not in the timer task of the button. */
static void reset_task(void *arg)
{
    if ((reset_t)(intptr_t)arg == RESET_FACTORY) {
        /* Everything in the "nvs" partition: settings, Wi-Fi network and (with HomeKit) the
         * pairings — what hap_reset_to_factory() erases too; the read-only factory_nvs
         * (setup code) stays. Wi-Fi is stopped first, so the driver does not write meanwhile. */
        ESP_LOGW(TAG, "menu held %d s: factory reset (all settings and the Wi-Fi network), restarting",
                 CONFIG_SH_FACTORY_RESET_HOLD_S);
        captive_wifi_stop();
        esp_err_t err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "menu released after %d s or more: erasing the stored Wi-Fi network, restarting",
                 CONFIG_SH_NET_RESET_HOLD_S);
        captive_wifi_clear_credentials();
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void start_reset(reset_t type)
{
    static bool started;            /* button callbacks all run in the timer task */
    if (!started) {
        started = true;
        xTaskCreate(reset_task, "sh_reset", 3072, (void *)(intptr_t)type, 5, NULL);
    }
}

/* Released after NET s (and before FACTORY s, else the factory reset has already started) */
static void on_menu_net_release(void *arg)
{
    (void)arg;
    start_reset(RESET_NETWORK);
}

static void on_menu_net_reached(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "menu held %d s: release now to reset the Wi-Fi network, "
             "keep holding until %d s for a factory reset", CONFIG_SH_NET_RESET_HOLD_S,
             CONFIG_SH_FACTORY_RESET_HOLD_S);
}

/* Still held after FACTORY s: starts at once, the release is not awaited */
static void on_menu_factory(void *arg)
{
    (void)arg;
    start_reset(RESET_FACTORY);
}

static button_handle_t add(gpio_num_t gpio, const char *name)
{
    button_handle_t b = iot_button_create(gpio, BUTTON_ACTIVE_LOW);
    if (b) {
        iot_button_set_evt_cb(b, BUTTON_CB_TAP, on_tap, (void *)name);
    }
    return b;
}

esp_err_t sh_buttons_init(void)
{
    button_handle_t menu  = add(SH_PIN_BTN_MENU, "menu");
    button_handle_t minus = add(SH_PIN_BTN_MINUS, "-");
    button_handle_t plus  = add(SH_PIN_BTN_PLUS, "+");
    if (!menu || !minus || !plus) {
        return ESP_FAIL;
    }
    esp_err_t err = iot_button_add_on_release_cb(menu, CONFIG_SH_NET_RESET_HOLD_S, on_menu_net_release, NULL);
    if (err == ESP_OK) {
        err = iot_button_add_on_press_cb(menu, CONFIG_SH_NET_RESET_HOLD_S, on_menu_net_reached, NULL);
    }
    if (err == ESP_OK) {
        err = iot_button_add_on_press_cb(menu, CONFIG_SH_FACTORY_RESET_HOLD_S, on_menu_factory, NULL);
    }
    return err;
}
