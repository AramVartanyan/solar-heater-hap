#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "sh_state.h"
#include "sh_pump.h"
#include "sh_relay.h"
#include "sh_led.h"
#include "sh_control.h"

static const char *TAG = "sh_control";

#define CONTROL_PERIOD_MS   1000

_Static_assert(CONFIG_SH_DT_OFF_K < CONFIG_SH_DT_ON_K,
               "Control: the switch-off difference must be lower than the switch-on difference");

/* Kconfig "Control": whole K / °C / minutes -> tenths and seconds */
static const sh_pump_params_t s_params = {
    .dt_on = CONFIG_SH_DT_ON_K * 10,
    .dt_off = CONFIG_SH_DT_OFF_K * 10,
    .min_run_s = CONFIG_SH_MIN_RUN_MIN * 60,
    .min_off_s = CONFIG_SH_MIN_OFF_MIN * 60,
    .tank_max = CONFIG_SH_TANK_MAX_C * 10,
    .col_min = CONFIG_SH_COL_MIN_C * 10,
    .col_protect = CONFIG_SH_COL_PROTECT_C * 10,
};

/* "23.4", "-0.5" or "---" for the log */
static const char *temp_str(char *buf, size_t size, int16_t t_tenths)
{
    if (t_tenths == SH_TEMP_INVALID) {
        return "---";
    }
    int mag = t_tenths < 0 ? -t_tenths : t_tenths;
    snprintf(buf, size, "%s%d.%d", t_tenths < 0 ? "-" : "", mag / 10, mag % 10);
    return buf;
}

static void control_task(void *arg)
{
    (void)arg;
    sh_pump_state_t pump = { 0 };
    int64_t changed_us = esp_timer_get_time();      /* monotonic; the pump is off since now */
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        sh_state_t st;
        sh_state_get(&st);

        const bool was_on = pump.on;
        const sh_pump_reason_t was_reason = pump.reason;
        const int64_t now_us = esp_timer_get_time();
        sh_pump_decide(&s_params, &pump, st.t_col, st.t_tank,
                       (uint32_t)((now_us - changed_us) / 1000000));

        if (pump.on != was_on) {
            changed_us = now_us;
            sh_relay_set(pump.on);
            char col[8], tank[8];
            ESP_LOGI(TAG, "pump %s (%s): collector %s °C, tank %s °C", pump.on ? "ON" : "OFF",
                     sh_pump_reason_name(pump.reason), temp_str(col, sizeof(col), st.t_col),
                     temp_str(tank, sizeof(tank), st.t_tank));
        }
        if (pump.on != was_on || pump.reason != was_reason) {
            sh_state_set_pump(pump.on, pump.reason);
        }

        sh_led_set(st.wifi_connected ? SH_COLOR_BLUE : SH_COLOR_OFF);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

esp_err_t sh_control_start(void)
{
#if CONFIG_SH_COL_PROTECT_C * 10 > SH_PUMP_COL_PROTECT_CAP
    ESP_LOGW(TAG, "collector protection %d °C is limited to %d °C", CONFIG_SH_COL_PROTECT_C,
             SH_PUMP_COL_PROTECT_CAP / 10);
#endif
    if (xTaskCreate(control_task, "sh_control", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "control task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
