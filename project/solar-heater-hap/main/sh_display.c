#include "esp_log.h"
#include "sh_config.h"
#include "sh_display.h"

static const char *TAG = "sh_display";

esp_err_t sh_display_init(void)
{
    ESP_LOGI(TAG, "TM1637: CLK GPIO%d, DIO GPIO%d (driver pending)",
             SH_PIN_TM1637_CLK, SH_PIN_TM1637_DIO);
    return ESP_OK;
}
