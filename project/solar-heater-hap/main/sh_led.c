#include "esp_log.h"
#include "led_strip.h"
#include "sh_config.h"
#include "sh_led.h"

static const char *TAG = "sh_led";

static led_strip_handle_t s_strip;
static sh_color_t s_color = (sh_color_t)-1;

static const uint8_t RGB[][3] = {
    [SH_COLOR_OFF]    = {   0,   0,   0 },
    [SH_COLOR_BLUE]   = {   0,   0, 255 },
    [SH_COLOR_GREEN]  = {   0, 255,   0 },
    [SH_COLOR_WHITE]  = { 255, 255, 255 },
    [SH_COLOR_YELLOW] = { 255, 160,   0 },
    [SH_COLOR_PURPLE] = { 160,   0, 255 },
    [SH_COLOR_RED]    = { 255,   0,   0 },
};

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
    sh_led_set(SH_COLOR_OFF);
    return ESP_OK;
}

void sh_led_set(sh_color_t color)
{
    if (!s_strip || color == s_color) {
        return;
    }
    s_color = color;
    const uint32_t k = CONFIG_SH_LED_BRIGHTNESS;     /* brightness scaling in our layer */
    led_strip_set_pixel(s_strip, 0, RGB[color][0] * k / 100, RGB[color][1] * k / 100,
                        RGB[color][2] * k / 100);
    led_strip_refresh(s_strip);
}
