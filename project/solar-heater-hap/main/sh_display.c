#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_tm1637.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_display.h"

static const char *TAG = "sh_display";

#define DIGITS          3
#define BRIGHTNESS      4               /* 0..7 */
#define SEG_MINUS       0x40

#define LABEL_MS        1000            /* label shown */
#define VALUE_MS        5000            /* temperature shown */
#define REFRESH_MS      250             /* a new reading appears within this time */

typedef enum { PAGE_TANK, PAGE_COL } page_t;

/* 3 digits: "Boil" does not fit, so the tank label is shortened */
static const struct {
    page_t page;
    const char *label;
} s_pages[] = {
    { PAGE_TANK, "boi" },
    { PAGE_COL,  "Col" },
};

static tm1637_led_t *s_disp;

static int16_t page_temp(page_t page)
{
    sh_state_t st;
    sh_state_get(&st);
    return page == PAGE_TANK ? st.t_tank : st.t_col;
}

static void show_temp(int16_t t_tenths)
{
    if (t_tenths == SH_TEMP_INVALID) {
        const uint8_t dashes[DIGITS] = { SEG_MINUS, SEG_MINUS, SEG_MINUS };
        tm1637_print_symbols(s_disp, dashes, DIGITS);
    } else {
        tm1637_print_temp(s_disp, t_tenths);
    }
}

/* Owns the display (esp_tm1637: one task per display). A display that does not answer is
 * retried by the library every 5 s; the writes meanwhile return at once. */
static void display_task(void *arg)
{
    (void)arg;
    for (;;) {
        for (size_t i = 0; i < sizeof(s_pages) / sizeof(s_pages[0]); i++) {
            tm1637_print_string(s_disp, s_pages[i].label);
            vTaskDelay(pdMS_TO_TICKS(LABEL_MS));

            int16_t shown = INT16_MIN;
            for (int ms = 0; ms < VALUE_MS; ms += REFRESH_MS) {
                int16_t t = page_temp(s_pages[i].page);
                if (t != shown) {               /* a new reading during the 5 s is shown too */
                    show_temp(t);
                    shown = t;
                }
                vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
            }
        }
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
