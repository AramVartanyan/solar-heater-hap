#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_tm1637.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_settings.h"
#include "sh_control.h"
#include "sh_menu.h"
#include "sh_display.h"

static const char *TAG = "sh_display";

#define DIGITS          SH_MENU_DIGITS
#define BRIGHTNESS      4               /* 0..7 */
#define SEG_MINUS       0x40

#define LABEL_MS        1000            /* label shown */
#define VALUE_MS        5000            /* temperature shown */
#define POLL_MS         50              /* keys are taken at once; the picture is checked this often */

/* Labels as raw segments (XGFEDCBA). "Boil" on 3 digits: "i" (segment E) and "l" (segments B, C)
 * share the last digit. */
static const uint8_t SEG_BOIL[DIGITS] = { 0x7C /* b */, 0x5C /* o */, 0x16 /* il */ };
static const uint8_t SEG_COL[DIGITS]  = { 0x39 /* C */, 0x5C /* o */, 0x30 /* l */ };
static const uint8_t SEG_NONE[DIGITS] = { SEG_MINUS, SEG_MINUS, SEG_MINUS };

/* What is on the display: raw segments, or a temperature printed by the library. */
typedef struct {
    bool    is_temp;
    int16_t temp;
    uint8_t seg[DIGITS];
} frame_t;

static tm1637_led_t *s_disp;
static QueueHandle_t s_keys;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void frame_seg(frame_t *f, const uint8_t seg[DIGITS])
{
    memset(f, 0, sizeof(*f));
    memcpy(f->seg, seg, DIGITS);
}

static void frame_temp(frame_t *f, int16_t t_tenths, bool fault)
{
    if (fault || t_tenths == SH_TEMP_INVALID) {
        frame_seg(f, SEG_NONE);
        return;
    }
    memset(f, 0, sizeof(*f));
    f->is_temp = true;
    f->temp = t_tenths;
}

/* Normal screen, `ms` since the cycle started at "Boil". */
static void frame_normal(frame_t *f, const sh_state_t *st, uint32_t ms)
{
    ms %= 2 * (LABEL_MS + VALUE_MS);
    if (ms < LABEL_MS) {
        frame_seg(f, SEG_BOIL);
    } else if (ms < LABEL_MS + VALUE_MS) {
        frame_temp(f, st->t_tank, st->tank_fault);
    } else if (ms < 2 * LABEL_MS + VALUE_MS) {
        frame_seg(f, SEG_COL);
    } else {
        frame_temp(f, st->t_col, st->col_fault);
    }
}

/* Pump test, `ms` since it appeared on the display. */
static void frame_test(frame_t *f, const sh_state_t *st, uint32_t ms)
{
    uint8_t seg[DIGITS];
    if (sh_menu_test_shows_label(ms)) {
        sh_menu_test_label(seg);
    } else {
        sh_menu_number(st->test_left_s, seg);
    }
    frame_seg(f, seg);
}

static void handle_key(sh_menu_t *menu, sh_key_t key, uint32_t now)
{
    /* On the normal screen the pump test comes first: "+" starts it, and while it runs "menu"
     * ends it instead of opening the menu. */
    if (menu->state == SH_MENU_OFF && sh_control_key(key)) {
        return;
    }
    sh_settings_t cur;
    sh_settings_get(&cur);
    sh_setting_id_t id;
    int16_t value;
    if (sh_menu_key(menu, key, now, &cur, &id, &value)) {
        sh_settings_set(id, value);     /* logs the result; the value is in use even if not stored */
    }
}

/* Owns the display (esp_tm1637: one task per display). A display that does not answer is
 * retried by the library every 5 s; the writes meanwhile return at once. */
static void display_task(void *arg)
{
    (void)arg;
    sh_menu_t menu = { 0 };
    uint32_t cycle_ms = now_ms();       /* the normal cycle started at "Boil" */
    uint32_t test_ms = 0;               /* the pump test appeared */
    bool test_shown = false;
    bool first = true;
    frame_t shown = { 0 };

    for (;;) {
        sh_key_t key;
        const bool pressed = xQueueReceive(s_keys, &key, pdMS_TO_TICKS(POLL_MS)) == pdTRUE;
        const uint32_t now = now_ms();

        const bool was_in_menu = menu.state != SH_MENU_OFF;
        sh_menu_tick(&menu, now);
        if (pressed) {
            handle_key(&menu, key, now);
        }
        if (was_in_menu && menu.state == SH_MENU_OFF) {
            cycle_ms = now;             /* back from the menu: the cycle starts at "Boil" */
        }

        sh_state_t st;
        sh_state_get(&st);
        frame_t f;
        if (menu.state != SH_MENU_OFF) {
            uint8_t seg[DIGITS];
            sh_menu_render(&menu, now, seg);
            frame_seg(&f, seg);
        } else if (st.test_active) {
            if (!test_shown) {
                test_shown = true;
                test_ms = now;
            }
            frame_test(&f, &st, now - test_ms);
        } else {
            if (test_shown) {
                test_shown = false;
                cycle_ms = now;         /* after the test as well */
            }
            frame_normal(&f, &st, now - cycle_ms);
        }

        if (first || memcmp(&f, &shown, sizeof(f)) != 0) {     /* written only when it changes */
            if (f.is_temp) {
                tm1637_print_temp(s_disp, f.temp);
            } else {
                tm1637_print_symbols(s_disp, f.seg, DIGITS);
            }
            memcpy(&shown, &f, sizeof(shown));
            first = false;
        }
    }
}

void sh_display_key(sh_key_t key)
{
    if (s_keys) {
        xQueueSend(s_keys, &key, 0);
    }
}

esp_err_t sh_display_init(void)
{
    s_disp = tm1637_init(SH_PIN_TM1637_CLK, SH_PIN_TM1637_DIO, DIGITS);
    if (!s_disp) {
        ESP_LOGE(TAG, "TM1637: out of memory");
        return ESP_ERR_NO_MEM;
    }
    tm1637_set_brightness(s_disp, BRIGHTNESS);
    tm1637_test_segments(s_disp);       /* all segments on for a moment */
    vTaskDelay(pdMS_TO_TICKS(500));
    tm1637_clear(s_disp);
    ESP_LOGI(TAG, "TM1637 (esp_tm1637 %s): CLK GPIO%d, DIO GPIO%d, %s", ESP_TM1637_VERSION,
             SH_PIN_TM1637_CLK, SH_PIN_TM1637_DIO,
             tm1637_is_online(s_disp) ? "answers" : "no answer");
    s_keys = xQueueCreate(8, sizeof(sh_key_t));
    if (!s_keys || xTaskCreate(display_task, "sh_display", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "display task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
