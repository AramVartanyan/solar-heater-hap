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

static tm1637_led_t *s_disp;
static int16_t s_shown = INT16_MIN;     /* nothing shown yet */

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
    return ESP_OK;
}

/* The display is written from the control task only (esp_tm1637: one task per display). */
void sh_display_temp(int16_t t_tenths)
{
    if (!s_disp) {
        return;
    }
    bool offline = !tm1637_is_online(s_disp);
    if (t_tenths == s_shown && !offline) {
        return;                         /* unchanged; an offline display is retried */
    }
    if (t_tenths == SH_TEMP_INVALID) {
        const uint8_t dashes[DIGITS] = { SEG_MINUS, SEG_MINUS, SEG_MINUS };
        tm1637_print_symbols(s_disp, dashes, DIGITS);
    } else {
        tm1637_print_temp(s_disp, t_tenths);
    }
    s_shown = t_tenths;
}
