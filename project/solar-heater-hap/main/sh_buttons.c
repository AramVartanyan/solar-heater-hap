#include "esp_log.h"
#include "iot_button.h"
#include "sh_config.h"
#include "sh_buttons.h"

static const char *TAG = "sh_buttons";

static void on_tap(void *arg)
{
    ESP_LOGI(TAG, "button %s", (const char *)arg);   /* menu logic comes later */
}

static esp_err_t add(gpio_num_t gpio, const char *name)
{
    button_handle_t b = iot_button_create(gpio, BUTTON_ACTIVE_LOW);   /* internal pull-up */
    if (!b) {
        return ESP_FAIL;
    }
    return iot_button_set_evt_cb(b, BUTTON_CB_TAP, on_tap, (void *)name);
}

esp_err_t sh_buttons_init(void)
{
    esp_err_t err = add(SH_PIN_BTN_MENU, "menu");
    if (err == ESP_OK) {
        err = add(SH_PIN_BTN_MINUS, "-");
    }
    if (err == ESP_OK) {
        err = add(SH_PIN_BTN_PLUS, "+");
    }
    return err;
}
