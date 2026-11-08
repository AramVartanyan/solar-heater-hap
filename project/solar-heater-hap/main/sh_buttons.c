#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
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

/* The button callbacks run in the FreeRTOS timer task (small stack, must not block): they only
 * queue an event. Everything else (log, menu, reset) runs in the buttons task. */
typedef enum {
    EVT_TAP_MENU,
    EVT_TAP_MINUS,
    EVT_TAP_PLUS,
    EVT_MENU_NET_REACHED,   /* held NET s: releasing now resets the Wi-Fi network */
    EVT_MENU_NET_RELEASE,   /* released after NET s (and before FACTORY s) */
    EVT_MENU_FACTORY,       /* still held after FACTORY s */
} btn_evt_t;

static QueueHandle_t s_queue;

static void post(void *arg)
{
    btn_evt_t evt = (btn_evt_t)(intptr_t)arg;
    xQueueSend(s_queue, &evt, 0);
}

static void reset_and_restart(bool factory)
{
    if (factory) {
        /* Everything in the "nvs" partition: settings, Wi-Fi network (kept there by the driver)
         * and, with HomeKit, the pairings — what hap_reset_to_factory() erases too; the read-only
         * factory_nvs (setup code) stays. Wi-Fi is stopped first, so the driver does not write
         * meanwhile. */
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

static void buttons_task(void *arg)
{
    (void)arg;
    btn_evt_t evt;
    for (;;) {
        if (xQueueReceive(s_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (evt) {
        case EVT_TAP_MENU:
            ESP_LOGI(TAG, "button menu");   /* menu logic comes later */
            break;
        case EVT_TAP_MINUS:
            ESP_LOGI(TAG, "button -");
            break;
        case EVT_TAP_PLUS:
            ESP_LOGI(TAG, "button +");
            break;
        case EVT_MENU_NET_REACHED:
            ESP_LOGI(TAG, "menu held %d s: release now to reset the Wi-Fi network, "
                     "keep holding until %d s for a factory reset", CONFIG_SH_NET_RESET_HOLD_S,
                     CONFIG_SH_FACTORY_RESET_HOLD_S);
            break;
        case EVT_MENU_NET_RELEASE:
            reset_and_restart(false);       /* does not return */
            break;
        case EVT_MENU_FACTORY:
            reset_and_restart(true);        /* does not return */
            break;
        }
    }
}

static button_handle_t add(gpio_num_t gpio, btn_evt_t tap)
{
    button_handle_t b = iot_button_create(gpio, BUTTON_ACTIVE_LOW);
    if (b) {
        iot_button_set_evt_cb(b, BUTTON_CB_TAP, post, (void *)(intptr_t)tap);
    }
    return b;
}

esp_err_t sh_buttons_init(void)
{
    s_queue = xQueueCreate(8, sizeof(btn_evt_t));
    if (!s_queue || xTaskCreate(buttons_task, "sh_buttons", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    button_handle_t menu  = add(SH_PIN_BTN_MENU, EVT_TAP_MENU);
    button_handle_t minus = add(SH_PIN_BTN_MINUS, EVT_TAP_MINUS);
    button_handle_t plus  = add(SH_PIN_BTN_PLUS, EVT_TAP_PLUS);
    if (!menu || !minus || !plus) {
        return ESP_FAIL;
    }
    esp_err_t err = iot_button_add_on_release_cb(menu, CONFIG_SH_NET_RESET_HOLD_S, post,
                                                 (void *)(intptr_t)EVT_MENU_NET_RELEASE);
    if (err == ESP_OK) {
        err = iot_button_add_on_press_cb(menu, CONFIG_SH_NET_RESET_HOLD_S, post,
                                         (void *)(intptr_t)EVT_MENU_NET_REACHED);
    }
    if (err == ESP_OK) {
        err = iot_button_add_on_press_cb(menu, CONFIG_SH_FACTORY_RESET_HOLD_S, post,
                                         (void *)(intptr_t)EVT_MENU_FACTORY);
    }
    return err;
}
