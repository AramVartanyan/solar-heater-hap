#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sh_state.h"
#include "sh_sensors.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_display.h"
#include "sh_control.h"

static const char *TAG = "sh_control";

#define CONTROL_PERIOD_MS   2000

static void control_task(void *arg)
{
    (void)arg;
    for (;;) {
        sh_sensors_read();
        sh_state_t st;
        sh_state_get(&st);
        /* pump logic pending (doc/led-indication.md, thresholds) — pump stays off */
        sh_relay_set(false);
        sh_led_set(st.wifi_connected ? SH_COLOR_BLUE : SH_COLOR_OFF);
        sh_display_temp(st.t_col);
        vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

esp_err_t sh_control_start(void)
{
    if (xTaskCreate(control_task, "sh_control", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "control task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
