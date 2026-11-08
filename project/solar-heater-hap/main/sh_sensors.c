#include "esp_log.h"
#include "sh_config.h"
#include "sh_state.h"
#include "sh_sensors.h"

static const char *TAG = "sh_sensors";

esp_err_t sh_sensors_init(void)
{
    ESP_LOGI(TAG, "DS18B20: collector GPIO%d, tank GPIO%d (driver pending)",
             SH_PIN_T_COL, SH_PIN_T_TANK);
    return ESP_OK;
}

void sh_sensors_read(void)
{
    sh_state_set_temps(SH_TEMP_INVALID, SH_TEMP_INVALID);
}
