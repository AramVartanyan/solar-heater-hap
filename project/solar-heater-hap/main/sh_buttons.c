#include <stdbool.h>
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
#include "sh_menu.h"
#include "sh_display.h"
#include "sh_buttons.h"

static const char *TAG = "sh_buttons";

#if CONFIG_SH_FACTORY_RESET_HOLD_S <= CONFIG_SH_NET_RESET_HOLD_S
#error "SH_FACTORY_RESET_HOLD_S must be longer than SH_NET_RESET_HOLD_S"
#endif

#define REPEAT_AFTER_S      1       /* "+" / "-" held this long: auto-repeat... */
#define REPEAT_PERIOD_MS    200     /* ...this often */

/* The button callbacks run in the FreeRTOS timer task (small stack, must not block): they only
 * queue an event. The keys of the menu and of the pump test go to the display task, which owns
 * both; the resets (log, erase, restart) run in the buttons task. */
typedef enum {
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

/* "+" and "-": a short press is reported when the button is released; held, it repeats and the
 * release is then not one more press (the library reports a tap after the repeats too). */
static bool s_repeated[2];          /* [0] "-", [1] "+" */

static void tap(void *arg)
{
    const sh_key_t key = (sh_key_t)(intptr_t)arg;
    if (key != SH_KEY_MENU) {
        bool *repeated = &s_repeated[key == SH_KEY_PLUS];
        if (*repeated) {
            *repeated = false;
            return;
        }
    }
    sh_display_key(key);
}

static void repeat(void *arg)
{
    const sh_key_t key = (sh_key_t)(intptr_t)arg;
    s_repeated[key == SH_KEY_PLUS_REPEAT] = true;
    sh_display_key(key);
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

static button_handle_t add(gpio_num_t gpio, sh_key_t key)
{
    button_handle_t b = iot_button_create(gpio, BUTTON_ACTIVE_LOW);
    if (b) {
        iot_button_set_evt_cb(b, BUTTON_CB_TAP, tap, (void *)(intptr_t)key);
    }
    return b;
}

esp_err_t sh_buttons_init(void)
{
    s_queue = xQueueCreate(8, sizeof(btn_evt_t));
    if (!s_queue || xTaskCreate(buttons_task, "sh_buttons", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    button_handle_t menu  = add(SH_PIN_BTN_MENU, SH_KEY_MENU);
    button_handle_t minus = add(SH_PIN_BTN_MINUS, SH_KEY_MINUS);
    button_handle_t plus  = add(SH_PIN_BTN_PLUS, SH_KEY_PLUS);
    if (!menu || !minus || !plus) {
        return ESP_FAIL;
    }
    esp_err_t err = iot_button_set_serial_cb(minus, REPEAT_AFTER_S, pdMS_TO_TICKS(REPEAT_PERIOD_MS),
                                             repeat, (void *)(intptr_t)SH_KEY_MINUS_REPEAT);
    if (err == ESP_OK) {
        err = iot_button_set_serial_cb(plus, REPEAT_AFTER_S, pdMS_TO_TICKS(REPEAT_PERIOD_MS),
                                       repeat, (void *)(intptr_t)SH_KEY_PLUS_REPEAT);
    }
    if (err == ESP_OK) {
        err = iot_button_add_on_release_cb(menu, CONFIG_SH_NET_RESET_HOLD_S, post,
                                           (void *)(intptr_t)EVT_MENU_NET_RELEASE);
    }
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
