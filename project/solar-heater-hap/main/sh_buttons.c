#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "iot_button.h"
#include "captive_wifi.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_settings_store.h"
#include "sh_control.h"
#include "sh_buttons.h"

static const char *TAG = "sh_buttons";

#if CONFIG_SH_FACTORY_RESET_HOLD_S <= CONFIG_SH_NET_RESET_HOLD_S
#error "SH_FACTORY_RESET_HOLD_S must be longer than SH_NET_RESET_HOLD_S"
#endif

#define REPEAT_AFTER_S      1       /* "+" / "-" held longer: repeated ... */
#define REPEAT_MS           200     /* ... every 200 ms */

/* The button callbacks run in the FreeRTOS timer task (small stack, must not block): they only
 * queue an event. Everything else (log, menu, reset) runs in the buttons task. */
typedef enum {
    EVT_TAP_MENU,
    EVT_TAP_MINUS,
    EVT_TAP_PLUS,
    EVT_PUSH_MINUS,         /* a new press (clears the "repeated" mark) */
    EVT_PUSH_PLUS,
    EVT_REPEAT_MINUS,       /* held > 1 s, every 200 ms */
    EVT_REPEAT_PLUS,
    EVT_MENU_NET_REACHED,   /* held NET s: releasing now resets the Wi-Fi network */
    EVT_MENU_NET_RELEASE,   /* released after NET s (and before FACTORY s) */
    EVT_MENU_FACTORY,       /* still held after FACTORY s */
} btn_evt_t;

static QueueHandle_t s_queue;

/* The menu: keys from this task, the view read by the display task. */
static sh_menu_t s_menu;
static SemaphoreHandle_t s_menu_lock;

/* The library reports a tap also on the release after a hold: a press that repeated is not a tap. */
static bool s_repeated_minus, s_repeated_plus;

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

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void menu_key(sh_key_t key)
{
    sh_settings_t settings;
    sh_settings_get(&settings);
    sh_state_t st;
    sh_state_get(&st);

    xSemaphoreTake(s_menu_lock, portMAX_DELAY);
    const sh_menu_act_t act = sh_menu_key(&s_menu, key, now_ms(), st.test_on, &settings);
    const sh_setting_t id = (sh_setting_t)s_menu.pos;
    const int16_t value = s_menu.value;
    xSemaphoreGive(s_menu_lock);

    switch (act) {
    case SH_MENU_ACT_PLUS:
        sh_control_cmd(SH_CONTROL_TEST_PLUS);
        break;
    case SH_MENU_ACT_MINUS:
        sh_control_cmd(SH_CONTROL_TEST_MINUS);
        break;
    case SH_MENU_ACT_STOP:
        sh_control_cmd(SH_CONTROL_TEST_STOP);
        break;
    case SH_MENU_ACT_SAVE:
        sh_settings_set(id, value);     /* logs the result */
        break;
    case SH_MENU_ACT_NONE:
        break;
    }
}

void sh_buttons_menu_view(sh_menu_view_t *out)
{
    if (!s_menu_lock) {                 /* buttons not started yet: the normal screen */
        *out = (sh_menu_view_t){ .mode = SH_MENU_NORMAL };
        return;
    }
    const uint32_t now = now_ms();
    xSemaphoreTake(s_menu_lock, portMAX_DELAY);
    sh_menu_update(&s_menu, now);
    sh_menu_view(&s_menu, now, out);
    xSemaphoreGive(s_menu_lock);
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
            ESP_LOGD(TAG, "button menu");
            menu_key(SH_KEY_MENU);
            break;
        case EVT_TAP_MINUS:
            ESP_LOGD(TAG, "button -");
            if (!s_repeated_minus) {
                menu_key(SH_KEY_MINUS);
            }
            break;
        case EVT_TAP_PLUS:
            ESP_LOGD(TAG, "button +");
            if (!s_repeated_plus) {
                menu_key(SH_KEY_PLUS);
            }
            break;
        case EVT_PUSH_MINUS:
            s_repeated_minus = false;
            break;
        case EVT_PUSH_PLUS:
            s_repeated_plus = false;
            break;
        case EVT_REPEAT_MINUS:
            s_repeated_minus = true;
            menu_key(SH_KEY_MINUS_REPEAT);
            break;
        case EVT_REPEAT_PLUS:
            s_repeated_plus = true;
            menu_key(SH_KEY_PLUS_REPEAT);
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

/* "+" / "-": a press, and the repetition while held (only the value in the menu uses it). */
static esp_err_t add_repeat(button_handle_t b, btn_evt_t push, btn_evt_t repeat)
{
    esp_err_t err = iot_button_set_evt_cb(b, BUTTON_CB_PUSH, post, (void *)(intptr_t)push);
    if (err == ESP_OK) {
        err = iot_button_set_serial_cb(b, REPEAT_AFTER_S, pdMS_TO_TICKS(REPEAT_MS), post,
                                       (void *)(intptr_t)repeat);
    }
    return err;
}

esp_err_t sh_buttons_init(void)
{
    s_queue = xQueueCreate(16, sizeof(btn_evt_t));
    s_menu_lock = xSemaphoreCreateMutex();
    if (!s_queue || !s_menu_lock ||
        xTaskCreate(buttons_task, "sh_buttons", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    button_handle_t menu  = add(SH_PIN_BTN_MENU, EVT_TAP_MENU);
    button_handle_t minus = add(SH_PIN_BTN_MINUS, EVT_TAP_MINUS);
    button_handle_t plus  = add(SH_PIN_BTN_PLUS, EVT_TAP_PLUS);
    if (!menu || !minus || !plus) {
        return ESP_FAIL;
    }
    esp_err_t err = add_repeat(minus, EVT_PUSH_MINUS, EVT_REPEAT_MINUS);
    if (err == ESP_OK) {
        err = add_repeat(plus, EVT_PUSH_PLUS, EVT_REPEAT_PLUS);
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
