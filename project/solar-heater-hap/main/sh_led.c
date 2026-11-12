#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "sh_config.h"
#include "sh_led.h"

static const char *TAG = "sh_led";

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock;        /* sh_led_show() and the timer callback are in different tasks */
static esp_timer_handle_t s_timer;      /* the next change of the light within the pattern */

static bool s_shown;                    /* something was asked for */
static sh_color_t s_color;              /* the colour asked for */
static sh_led_pattern_t s_pattern;      /* the pattern asked for */
static int64_t s_start_us;              /* start of the pattern */
static sh_color_t s_light = SH_COLOR_COUNT;     /* what the LED shows now (none yet) */

static const uint8_t RGB[SH_COLOR_COUNT][3] = {
    [SH_COLOR_OFF]    = {   0,   0,   0 },
    [SH_COLOR_BLUE]   = {   0,   0, 255 },
    [SH_COLOR_GREEN]  = {   0, 255,   0 },
    [SH_COLOR_WHITE]  = { 255, 255, 255 },
    [SH_COLOR_YELLOW] = { 255, 160,   0 },
    [SH_COLOR_ORANGE] = { 255,  60,   0 },
    [SH_COLOR_PURPLE] = { 160,   0, 255 },
    [SH_COLOR_RED]    = { 255,   0,   0 },
};

/* Writes the LED only when its light changes. */
static void light(sh_color_t color)
{
    if (color == s_light) {
        return;
    }
    s_light = color;
    const uint32_t k = CONFIG_SH_LED_BRIGHTNESS;     /* brightness scaling in our layer */
    led_strip_set_pixel(s_strip, 0, RGB[color][0] * k / 100, RGB[color][1] * k / 100,
                        RGB[color][2] * k / 100);
    led_strip_refresh(s_strip);
}

/* The light of this moment by the time since the start of the pattern (no drift, and an extra call
 * changes nothing), and the timer for its next change. Called with the lock taken. */
static void update(void)
{
    const uint32_t period = sh_led_pattern_period(s_pattern);
    const uint64_t ms = (uint64_t)(esp_timer_get_time() - s_start_us) / 1000;
    uint32_t next_ms;
    const bool lit = sh_led_pattern_lit(s_pattern, period ? (uint32_t)(ms % period) : 0, &next_ms);
    light(lit ? s_color : SH_COLOR_OFF);
    esp_timer_stop(s_timer);            /* not running: an error, ignored */
    if (next_ms) {
        esp_timer_start_once(s_timer, (uint64_t)next_ms * 1000);
    }
}

static void timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    update();
    xSemaphoreGive(s_lock);
}

esp_err_t sh_led_init(void)
{
    const led_strip_config_t strip_cfg = {
        .strip_gpio_num = SH_PIN_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  /* 0.1 us step */
        .mem_block_symbols = 0,             /* default minimum */
        .flags.with_dma = false,            /* not needed for one LED */
    };
    led_strip_handle_t strip;
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip: %s", esp_err_to_name(err));
        return err;
    }
    const esp_timer_create_args_t timer_args = {
        .callback = timer_cb,
        .name = "sh_led",
    };
    s_lock = xSemaphoreCreateMutex();
    err = s_lock ? esp_timer_create(&timer_args, &s_timer) : ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pattern timer: %s", esp_err_to_name(err));
        return err;
    }
    s_strip = strip;
    light(SH_COLOR_OFF);
    return ESP_OK;
}

void sh_led_show(sh_color_t color, sh_led_pattern_t pattern)
{
    if (!s_strip || (unsigned)color >= SH_COLOR_COUNT) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_shown || color != s_color || pattern != s_pattern) {
        if (!s_shown || pattern != s_pattern) {
            s_start_us = esp_timer_get_time();  /* a new colour keeps the rhythm of the pattern */
        }
        s_shown = true;
        s_color = color;
        s_pattern = pattern;
        update();
    }
    xSemaphoreGive(s_lock);
}
