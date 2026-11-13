#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "sh_config.h"
#include "sh_led.h"

static const char *TAG = "sh_led";

#define TICK_US     10000       /* every pattern time is a multiple of 10 ms */

static led_strip_handle_t s_strip;
static esp_timer_handle_t s_timer;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* What should be shown (written by sh_led_show(), read by the timer) */
static sh_color_t s_color = SH_COLOR_OFF;
static sh_ind_pattern_t s_pattern = SH_IND_STEADY;
static int64_t s_start_us;

/* What the LED shows (only the timer callback writes the LED) */
static sh_color_t s_shown = SH_COLOR_COUNT;

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

static void led_write(sh_color_t color)
{
    const uint32_t k = CONFIG_SH_LED_BRIGHTNESS;     /* brightness scaling in our layer */
    led_strip_set_pixel(s_strip, 0, RGB[color][0] * k / 100, RGB[color][1] * k / 100,
                        RGB[color][2] * k / 100);
    led_strip_refresh(s_strip);
}

static void tick(void *arg)
{
    (void)arg;
    taskENTER_CRITICAL(&s_lock);
    const sh_color_t color = s_color;
    const sh_ind_pattern_t pattern = s_pattern;
    const int64_t start_us = s_start_us;
    taskEXIT_CRITICAL(&s_lock);

    const uint32_t ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    const sh_color_t now = sh_ind_lit(pattern, ms) ? color : SH_COLOR_OFF;
    if (now != s_shown) {
        led_write(now);
        s_shown = now;
    }
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
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip: %s", esp_err_to_name(err));
        return err;
    }
    led_write(SH_COLOR_OFF);
    s_shown = SH_COLOR_OFF;

    const esp_timer_create_args_t args = { .callback = tick, .name = "sh_led" };
    err = esp_timer_create(&args, &s_timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(s_timer, TICK_US);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer: %s", esp_err_to_name(err));
    }
    return err;
}

void sh_led_show(sh_color_t color, sh_ind_pattern_t pattern)
{
    if ((unsigned)color >= SH_COLOR_COUNT) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    if (pattern != s_pattern) {
        s_start_us = esp_timer_get_time();
        s_pattern = pattern;
    }
    s_color = color;
    taskEXIT_CRITICAL(&s_lock);
}
