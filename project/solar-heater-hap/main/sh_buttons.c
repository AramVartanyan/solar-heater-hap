#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "iot_button.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_settings_store.h"
#include "sh_control.h"
#include "sh_reset.h"
#include "sh_net.h"
#include "sh_hap.h"
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
    EVT_RELEASE_MINUS,
    EVT_RELEASE_PLUS,
    EVT_LOCKED,             /* the keyboard lock has changed (any source) */
    EVT_UNLOCKED,
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

/* "+" and "-" held together: 3 s lock / unlock; such a press is never a "+" or a "-". */
static sh_chord_t s_chord;
#define CHORD_POLL_MS   50      /* while both are held */

static void post(void *arg)
{
    btn_evt_t evt = (btn_evt_t)(intptr_t)arg;
    xQueueSend(s_queue, &evt, 0);
}

static void reset_and_restart(bool factory)
{
    if (factory) {
        ESP_LOGW(TAG, "menu held %d s: factory reset", CONFIG_SH_FACTORY_RESET_HOLD_S);
    } else {
        ESP_LOGW(TAG, "menu released after %d s or more: Wi-Fi reset", CONFIG_SH_NET_RESET_HOLD_S);
    }
    sh_reset_now(factory ? SH_RESET_FACTORY : SH_RESET_WIFI);   /* does not return */
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void get_ctx(sh_menu_ctx_t *ctx)
{
    sh_state_t st;
    sh_state_get(&st);
    *ctx = (sh_menu_ctx_t){
        .test_running = st.test_on, .air_running = st.modes.air_on, .winter = st.modes.winter,
        .net_ap = st.net_ap, .net_connected = st.wifi_connected, .fup_offer = st.fup_offer,
        .hap_on = sh_hap_enabled(), .hap_paired = sh_hap_paired(), .locked = st.hap_lock,
    };
}

/* A key, or (`chord`) "+" and "-" held together for 3 s. */
static void menu_key(sh_key_t key, bool chord)
{
    sh_settings_t settings;
    sh_settings_get(&settings);
    sh_menu_ctx_t ctx;
    get_ctx(&ctx);

    xSemaphoreTake(s_menu_lock, portMAX_DELAY);
    const sh_menu_act_t act = chord ? sh_menu_chord(&s_menu, &ctx)
                                    : sh_menu_key(&s_menu, key, now_ms(), &ctx, &settings);
    const sh_setting_t id = (sh_setting_t)s_menu.id;
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
    case SH_MENU_ACT_AIR_STOP:
        sh_control_cmd(SH_CONTROL_AIR_STOP);
        break;
    case SH_MENU_ACT_SAVE:
        sh_settings_set(id, value);     /* logs the result */
        break;
    case SH_MENU_ACT_NET:
        ESP_LOGI(TAG, "menu: nEt -> %s", value ? "AP" : "CP");
        sh_net_set_mode(value ? SH_NET_AP : SH_NET_CP);
        break;
    case SH_MENU_ACT_FUP:
        ESP_LOGI(TAG, "menu: firmware update confirmed");
        sh_net_update(SH_OTA_CMD_CONFIRM);
        break;
    case SH_MENU_ACT_HAP:
        ESP_LOGI(TAG, "menu: HAP -> %s", value ? "on" : "oFF");
        sh_hap_set_enabled(value != 0);     /* stores it and restarts */
        break;
    case SH_MENU_ACT_LOCK:
        sh_hap_set_lock(true, "the buttons");       /* "LoC" comes back through sh_buttons_lock_changed() */
        break;
    case SH_MENU_ACT_UNLOCK:
        sh_hap_set_lock(false, "the buttons");
        break;
    case SH_MENU_ACT_NONE:
        break;
    }
}

/* "menu" held for a reset: not with the keyboard locked ("LoC"). */
static bool hold_allowed(void)
{
    sh_menu_ctx_t ctx;
    get_ctx(&ctx);
    xSemaphoreTake(s_menu_lock, portMAX_DELAY);
    const bool ok = sh_menu_hold(&s_menu, now_ms(), &ctx);
    xSemaphoreGive(s_menu_lock);
    return ok;
}

void sh_buttons_lock_changed(bool locked)
{
    if (s_queue) {
        const btn_evt_t evt = locked ? EVT_LOCKED : EVT_UNLOCKED;
        xQueueSend(s_queue, &evt, 0);
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
        /* while "+" and "-" are held together their 3 s are watched */
        const bool got = xQueueReceive(s_queue, &evt, sh_chord_holding(&s_chord) ? pdMS_TO_TICKS(CHORD_POLL_MS)
                                                                                : portMAX_DELAY) == pdTRUE;
        if (sh_chord_step(&s_chord, now_ms())) {
            ESP_LOGI(TAG, "+ and - held %u s", SH_MENU_CHORD_MS / 1000u);
            menu_key(SH_KEY_MENU, true);
        }
        if (!got) {
            continue;
        }
        switch (evt) {
        case EVT_TAP_MENU:
            ESP_LOGD(TAG, "button menu");
            menu_key(SH_KEY_MENU, false);
            break;
        case EVT_TAP_MINUS:
            ESP_LOGD(TAG, "button -");
            if (!s_repeated_minus && !sh_chord_swallow(&s_chord, false)) {
                menu_key(SH_KEY_MINUS, false);
            }
            break;
        case EVT_TAP_PLUS:
            ESP_LOGD(TAG, "button +");
            if (!s_repeated_plus && !sh_chord_swallow(&s_chord, true)) {
                menu_key(SH_KEY_PLUS, false);
            }
            break;
        case EVT_PUSH_MINUS:
            s_repeated_minus = false;
            sh_chord_button(&s_chord, false, true, now_ms());
            break;
        case EVT_PUSH_PLUS:
            s_repeated_plus = false;
            sh_chord_button(&s_chord, true, true, now_ms());
            break;
        case EVT_RELEASE_MINUS:
            sh_chord_button(&s_chord, false, false, now_ms());
            break;
        case EVT_RELEASE_PLUS:
            sh_chord_button(&s_chord, true, false, now_ms());
            break;
        case EVT_REPEAT_MINUS:
            s_repeated_minus = true;
            if (!sh_chord_swallow(&s_chord, false)) {
                menu_key(SH_KEY_MINUS_REPEAT, false);
            }
            break;
        case EVT_REPEAT_PLUS:
            s_repeated_plus = true;
            if (!sh_chord_swallow(&s_chord, true)) {
                menu_key(SH_KEY_PLUS_REPEAT, false);
            }
            break;
        case EVT_LOCKED:
        case EVT_UNLOCKED:
            xSemaphoreTake(s_menu_lock, portMAX_DELAY);
            sh_menu_lock_changed(&s_menu, evt == EVT_LOCKED, now_ms());
            xSemaphoreGive(s_menu_lock);
            break;
        case EVT_MENU_NET_REACHED:
            if (hold_allowed()) {
                ESP_LOGI(TAG, "menu held %d s: release now to reset the Wi-Fi network, "
                         "keep holding until %d s for a factory reset", CONFIG_SH_NET_RESET_HOLD_S,
                         CONFIG_SH_FACTORY_RESET_HOLD_S);
            } else {
                ESP_LOGI(TAG, "menu held %d s: the keyboard is locked, no reset", CONFIG_SH_NET_RESET_HOLD_S);
            }
            break;
        case EVT_MENU_NET_RELEASE:
            if (hold_allowed()) {
                reset_and_restart(false);   /* does not return */
            }
            break;
        case EVT_MENU_FACTORY:
            if (hold_allowed()) {
                reset_and_restart(true);    /* does not return */
            }
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

/* "+" / "-": a press and a release (the two held together), and the repetition while held (only
 * the value in the menu uses it). */
static esp_err_t add_repeat(button_handle_t b, btn_evt_t push, btn_evt_t release, btn_evt_t repeat)
{
    esp_err_t err = iot_button_set_evt_cb(b, BUTTON_CB_PUSH, post, (void *)(intptr_t)push);
    if (err == ESP_OK) {
        err = iot_button_set_evt_cb(b, BUTTON_CB_RELEASE, post, (void *)(intptr_t)release);
    }
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
    esp_err_t err = add_repeat(minus, EVT_PUSH_MINUS, EVT_RELEASE_MINUS, EVT_REPEAT_MINUS);
    if (err == ESP_OK) {
        err = add_repeat(plus, EVT_PUSH_PLUS, EVT_RELEASE_PLUS, EVT_REPEAT_PLUS);
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
