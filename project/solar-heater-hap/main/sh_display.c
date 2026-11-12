#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tm1637.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_alarm.h"
#include "sh_menu.h"
#include "sh_buttons.h"
#include "sh_display.h"

static const char *TAG = "sh_display";

#define DIGITS          3
#define BRIGHTNESS      4               /* 0..7 */
#define SEG_MINUS       0x40
#define SEG_DP          0x80

#define LABEL_MS        1000            /* normal screen: label shown */
#define VALUE_MS        5000            /* normal screen: temperature shown */
#define CYCLE_MS        (2 * (LABEL_MS + VALUE_MS))     /* Boil, tank, Col, collector */
#define TICK_MS         50              /* a change appears within this time */

/* Raw segments (XGFEDCBA) */
static const uint8_t s_digit[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

/* "Boil" on 3 digits: "i" (segment E) and "l" (segments B, C) share the last digit. */
static const uint8_t s_boil[DIGITS] = { 0x7C /* b */, 0x5C /* o */, 0x16 /* il */ };
static const uint8_t s_col[DIGITS]  = { 0x39 /* C */, 0x5C /* o */, 0x30 /* l */ };
static const uint8_t s_tst[DIGITS]  = { 0x78 /* t */, 0x6D /* S */, 0x78 /* t */ };

/* Alarm and information codes, in the order of sh_alarm_t: "E1" ... right-aligned, "Hot", "Fro" */
static const uint8_t s_codes[SH_ALARM_COUNT][DIGITS] = {
    [SH_ALARM_E1]  = { 0, 0x79 /* E */, 0x06 /* 1 */ },
    [SH_ALARM_E2]  = { 0, 0x79 /* E */, 0x5B /* 2 */ },
    [SH_ALARM_E4]  = { 0, 0x79 /* E */, 0x66 /* 4 */ },
    [SH_ALARM_E5]  = { 0, 0x79 /* E */, 0x6D /* 5 */ },
    [SH_ALARM_E6]  = { 0, 0x79 /* E */, 0x7D /* 6 */ },
    [SH_ALARM_HOT] = { 0x76 /* H */, 0x5C /* o */, 0x78 /* t */ },
    [SH_ALARM_FRO] = { 0x71 /* F */, 0x50 /* r */, 0x5C /* o */ },
};

/* Menu items, in the order of sh_setting_t, then End */
static const uint8_t s_items[SH_MENU_ITEMS][DIGITS] = {
    [SH_SET_TANK_MAX] = { 0x78 /* t */, 0x7C /* b */, 0x5C /* o */ },
    [SH_SET_COL_MIN]  = { 0x39 | SEG_DP /* C. */, 0x38 /* L */, 0x5C /* o */ },
    [SH_SET_DIFF_ON]  = { 0x5E | SEG_DP /* d. */, 0x5C /* o */, 0x54 /* n */ },
    [SH_SET_DIFF_OFF] = { 0x5E | SEG_DP /* d. */, 0x5C /* o */, 0x71 /* F */ },
    [SH_SET_TEST_S]   = { 0x78 /* t */, 0x6D /* S */, 0x78 /* t */ },
    [SH_MENU_END]     = { 0x79 /* E */, 0x54 /* n */, 0x5E /* d */ },
};

/* What is on the display: raw segments, or a temperature (printed by the library). */
typedef struct {
    bool is_temp;
    int16_t temp;
    uint8_t seg[DIGITS];
} screen_t;

static tm1637_led_t *s_disp;

static void set_seg(screen_t *s, const uint8_t seg[DIGITS])
{
    s->is_temp = false;
    memcpy(s->seg, seg, DIGITS);
}

/* Whole number 0..999, right-aligned */
static void set_number(screen_t *s, int value)
{
    s->is_temp = false;
    memset(s->seg, 0, DIGITS);
    if (value < 0) {
        value = 0;
    }
    if (value > 999) {
        value = 999;
    }
    for (int i = DIGITS - 1; i >= 0; i--) {
        s->seg[i] = s_digit[value % 10];
        value /= 10;
        if (value == 0) {
            break;
        }
    }
}

static void set_temp(screen_t *s, int16_t t_tenths, bool fault)
{
    if (fault || t_tenths == SH_TEMP_INVALID) {
        const uint8_t dashes[DIGITS] = { SEG_MINUS, SEG_MINUS, SEG_MINUS };
        set_seg(s, dashes);
    } else {
        s->is_temp = true;
        s->temp = t_tenths;
    }
}

static void show(const screen_t *s)
{
    if (s->is_temp) {
        tm1637_print_temp(s_disp, s->temp);
    } else {
        tm1637_print_symbols(s_disp, s->seg, DIGITS);
    }
}

static bool same(const screen_t *a, const screen_t *b)
{
    if (a->is_temp != b->is_temp) {
        return false;
    }
    return a->is_temp ? a->temp == b->temp : memcmp(a->seg, b->seg, DIGITS) == 0;
}

static void menu_screen(screen_t *s, const sh_menu_view_t *v)
{
    if (v->mode == SH_MENU_LIST) {
        set_seg(s, s_items[v->pos < SH_MENU_ITEMS ? v->pos : SH_MENU_END]);
    } else if (v->value_visible) {
        set_number(s, v->value);
    } else {
        const uint8_t blank[DIGITS] = { 0 };
        set_seg(s, blank);
    }
}

static void test_screen(screen_t *s, const sh_state_t *st, uint32_t now_s)
{
    const uint32_t elapsed = now_s - st->test_start_s;
    if (sh_menu_test_label(elapsed)) {
        set_seg(s, s_tst);
    } else {
        set_number(s, st->test_end_s > now_s ? (int)(st->test_end_s - now_s) : 0);
    }
}

/* Normal screen, `ms` after the start of the cycle: Boil, tank, Col, collector, then the active
 * codes (1 s each). Returns false when the cycle is over (the caller starts it again). */
static bool normal_screen(screen_t *s, const sh_state_t *st, uint32_t ms)
{
    if (ms >= CYCLE_MS) {
        const sh_alarm_t code = sh_alarm_code_at(st->alarms, ms - CYCLE_MS);
        if (code == SH_ALARM_COUNT) {
            return false;
        }
        set_seg(s, s_codes[code]);
    } else if (ms < LABEL_MS) {
        set_seg(s, s_boil);
    } else if (ms < LABEL_MS + VALUE_MS) {
        set_temp(s, st->t_tank, st->tank_fault);
    } else if (ms < 2 * LABEL_MS + VALUE_MS) {
        set_seg(s, s_col);
    } else {
        set_temp(s, st->t_col, st->col_fault);
    }
    return true;
}

/* Owns the display (esp_tm1637: one task per display); writes only when the screen changes.
 * A display that does not answer is retried by the library every 5 s; the writes meanwhile
 * return at once. */
static void display_task(void *arg)
{
    (void)arg;
    screen_t shown = { 0 };
    bool written = false;
    bool was_normal = true, was_test = false;
    uint32_t cycle_ms = (uint32_t)(esp_timer_get_time() / 1000);

    for (;;) {
        const int64_t us = esp_timer_get_time();
        const uint32_t now_ms = (uint32_t)(us / 1000);
        const uint32_t now_s = (uint32_t)(us / 1000000);
        sh_menu_view_t view;
        sh_buttons_menu_view(&view);
        sh_state_t st;
        sh_state_get(&st);

        screen_t scr = { 0 };
        const bool normal = view.mode == SH_MENU_NORMAL;
        if (!normal) {
            menu_screen(&scr, &view);   /* no alarms or test in the menu */
        } else {
            if (!was_normal || (was_test && !st.test_on)) {
                cycle_ms = now_ms;      /* back to the normal screen: from "Boil" */
            }
            if (st.test_on) {
                test_screen(&scr, &st, now_s);
            } else if (!normal_screen(&scr, &st, now_ms - cycle_ms)) {
                cycle_ms = now_ms;      /* the codes are over: from "Boil" again */
                normal_screen(&scr, &st, 0);
            }
            was_test = st.test_on;
        }
        was_normal = normal;

        if (!written || !same(&scr, &shown)) {
            show(&scr);
            shown = scr;
            written = true;
        }
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
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
    if (xTaskCreate(display_task, "sh_display", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "display task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
