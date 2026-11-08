#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "iot_button.h"
#include "captive_wifi.h"
#include "sh_config.h"
#include "sh_buttons.h"

static const char *TAG = "sh_buttons";

static void on_tap(void *arg)
{
    ESP_LOGI(TAG, "button %s", (const char *)arg);   /* menu logic comes later */
}

/* Erasing NVS blocks, so it runs in its own short task, not in the timer task of the button. */
static void net_reset_task(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "menu held %d s: erasing the stored Wi-Fi network, restarting",
             CONFIG_SH_NET_RESET_HOLD_S);
    captive_wifi_clear_credentials();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void on_menu_hold(void *arg)
{
    static bool started;
    (void)arg;
    if (!started) {
        started = true;
        xTaskCreate(net_reset_task, "net_reset", 3072, NULL, 5, NULL);
    }
}

static button_handle_t add(gpio_num_t gpio, const char *name)
{
    button_handle_t b = iot_button_create(gpio, BUTTON_ACTIVE_LOW);   /* internal pull-up */
    if (b) {
        iot_button_set_evt_cb(b, BUTTON_CB_TAP, on_tap, (void *)name);
    }
    return b;
}

esp_err_t sh_buttons_init(void)
{
    button_handle_t menu = add(SH_PIN_BTN_MENU, "menu");
    button_handle_t minus = add(SH_PIN_BTN_MINUS, "-");
    button_handle_t plus = add(SH_PIN_BTN_PLUS, "+");
    if (!menu || !minus || !plus) {
        return ESP_FAIL;
    }
    /* temporary until the menu exists: hold "menu" to reset the Wi-Fi network */
    return iot_button_add_on_press_cb(menu, CONFIG_SH_NET_RESET_HOLD_S, on_menu_hold, NULL);
}
